#!/usr/bin/env python

import argparse
import collections
import logging
import os
import shutil
import sys
import subprocess
import tempfile
import threading
import time

import cdec
import aligner
import decoder
import util

# Dummy input token that is unlikely to appear in normalized data (but no fatal errors if it does)
LIKELY_OOV = '(OOV)'

class RealtimeDecoder:
    '''Do not use directly unless you know what you're doing.  Use RealtimeTranslator.'''

    def __init__(self, configdir, tmpdir):

        cdec_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

        self.tmp = tmpdir
        os.mkdir(self.tmp)

        # HPYPLM reference stream
        ref_fifo_file = os.path.join(self.tmp, 'ref.fifo')
        os.mkfifo(ref_fifo_file)
        self.ref_fifo = open(ref_fifo_file, 'w+')
        # Start with empty line (do not learn prior to first input)
        self.ref_fifo.write('\n')
        self.ref_fifo.flush()

        # Decoder
        decoder_config = [[f.strip() for f in line.split('=')] for line in open(os.path.join(configdir, 'cdec.ini'))]
        util.cdec_ini_for_realtime(decoder_config, os.path.abspath(configdir), ref_fifo_file)
        decoder_config_file = os.path.join(self.tmp, 'cdec.ini')
        with open(decoder_config_file, 'w') as output:
            for (k, v) in decoder_config:
                output.write('{}={}\n'.format(k, v))
        decoder_weights = os.path.join(configdir, 'weights.final')
        self.decoder = decoder.MIRADecoder(decoder_config_file, decoder_weights)

    def close(self, force=False):
        logging.info('Closing decoder and removing {}'.format(self.tmp))
        self.decoder.close(force)
        self.ref_fifo.close()
        shutil.rmtree(self.tmp)

class RealtimeTranslator:
    '''Main entry point into API: serves translations to any number of concurrent users'''

    def __init__(self, configdir, tmpdir='/tmp', cache_size=5, norm=False, state=None):

        # name -> (method, set of possible nargs)
        self.COMMANDS = {
                'TR': (self.translate, set((1,))),
                'LEARN': (self.learn, set((2,))),
                'SAVE': (self.save_state, set((0, 1))),
                'LOAD': (self.load_state, set((0, 1))),
                'DROP': (self.drop_ctx, set((0,))),
                'LIST': (self.list_ctx, set((0,))),
                }
        
        cdec_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

        ### Single instance for all contexts

        self.config = configdir
        # Temporary work dir
        self.tmp = tempfile.mkdtemp(dir=tmpdir, prefix='realtime.')
        logging.info('Using temp dir {}'.format(self.tmp))

        # Normalization
        self.norm = norm
        if self.norm:
            self.tokenizer = util.popen_io([os.path.join(cdec_root, 'corpus', 'tokenize-anything.sh'), '-u'])
            self.tokenizer_lock = util.FIFOLock()
            self.detokenizer = util.popen_io([os.path.join(cdec_root, 'corpus', 'untok.pl')])
            self.detokenizer_lock = util.FIFOLock()

        # Word aligner
        fwd_params = os.path.join(configdir, 'a.fwd_params')
        fwd_err = os.path.join(configdir, 'a.fwd_err')
        rev_params = os.path.join(configdir, 'a.rev_params')
        rev_err = os.path.join(configdir, 'a.rev_err')
        self.aligner = aligner.ForceAligner(fwd_params, fwd_err, rev_params, rev_err)

        # Grammar extractor
        sa_config = cdec.configobj.ConfigObj(os.path.join(configdir, 'sa.ini'), unrepr=True)
        sa_config.filename = os.path.join(self.tmp, 'sa.ini')
        util.sa_ini_for_realtime(sa_config, os.path.abspath(configdir))
        sa_config.write()
        self.extractor = cdec.sa.GrammarExtractor(sa_config.filename, online=True)
        self.cache_size = cache_size

        ### One instance per context

        self.ctx_names = set()
        # All context-dependent operations are atomic
        self.ctx_locks = collections.defaultdict(util.FIFOLock)
        # ctx -> list of (source, target, alignment)
        self.ctx_data = {}

        # Grammar extractor is not threadsafe
        self.extractor_lock = util.FIFOLock()
        # ctx -> deque of file
        self.grammar_files = {}
        # ctx -> dict of {sentence: file}
        self.grammar_dict = {}

        self.decoders = {}

        # Load state if given
        if state:
            with open(state) as input:
                self.load_state(input)

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_traceback):
        # Force shutdown on exception
        self.close(ex_type is not None)

    def close(self, force=False):
        '''Cleanup'''
        if force:
            logging.info('Forced shutdown: stopping immediately')
        for ctx_name in list(self.ctx_names):
            self.drop_ctx(ctx_name, force)
        logging.info('Closing processes')
        self.aligner.close(force)
        if self.norm:
            if not force:
                self.tokenizer_lock.acquire()
                self.detokenizer_lock.acquire()
            self.tokenizer.stdin.close()
            self.detokenizer.stdin.close()
            if not force:
                self.tokenizer_lock.release()
                self.detokenizer_lock.release()
        logging.info('Deleting {}'.format(self.tmp))
        shutil.rmtree(self.tmp)

    def lazy_ctx(self, ctx_name):
        '''Initialize a context (inc starting a new decoder) if needed.
        NOT threadsafe, acquire ctx_name lock before calling.'''
        if ctx_name in self.ctx_names:
            return
        logging.info('New context: {}'.format(ctx_name))
        self.ctx_names.add(ctx_name)
        self.ctx_data[ctx_name] = []
        self.grammar_files[ctx_name] = collections.deque()
        self.grammar_dict[ctx_name] = {}
        tmpdir = os.path.join(self.tmp, 'decoder.{}'.format(ctx_name))
        self.decoders[ctx_name] = RealtimeDecoder(self.config, tmpdir)

    def drop_ctx(self, ctx_name=None, force=False):
        '''Delete a context (inc stopping the decoder)
        Threadsafe and FIFO unless forced.'''
        lock = self.ctx_locks[ctx_name]
        if not force:
            lock.acquire()
        if ctx_name not in self.ctx_names:
            logging.info('No context found, no action: {}'.format(ctx_name))
            if not force:
                lock.release()
            return
        logging.info('Dropping context: {}'.format(ctx_name))
        self.ctx_names.remove(ctx_name)
        self.ctx_data.pop(ctx_name)
        self.extractor.drop_ctx(ctx_name)
        self.grammar_files.pop(ctx_name)
        self.grammar_dict.pop(ctx_name)
        self.decoders.pop(ctx_name).close(force)
        self.ctx_locks.pop(ctx_name)
        if not force:
            lock.release()

    def list_ctx(self, ctx_name=None):
        '''Return a string of active contexts'''
        return 'ctx_name ||| {}'.format(' '.join(sorted(str(ctx_name) for ctx_name in self.ctx_names)))

    def grammar(self, sentence, ctx_name=None):
        '''Extract a sentence-level grammar on demand (or return cached)
        Threadsafe wrt extractor but NOT decoder.  Acquire ctx_name lock
        before calling.'''
        self.extractor_lock.acquire()
        self.lazy_ctx(ctx_name)
        grammar_dict = self.grammar_dict[ctx_name]
        grammar_file = grammar_dict.get(sentence, None)
        # Cache hit
        if grammar_file:
            logging.info('Grammar cache hit: {}'.format(grammar_file))
            self.extractor_lock.release()
            return grammar_file
        # Extract and cache
        (fid, grammar_file) = tempfile.mkstemp(dir=self.decoders[ctx_name].tmp, prefix='grammar.')
        os.close(fid)
        with open(grammar_file, 'w') as output:
            for rule in self.extractor.grammar(sentence, ctx_name):
                output.write('{}\n'.format(str(rule)))
        grammar_files = self.grammar_files[ctx_name]
        if len(grammar_files) == self.cache_size:
            rm_sent = grammar_files.popleft()
            # If not already removed by learn method
            if rm_sent in grammar_dict:
                rm_grammar = grammar_dict.pop(rm_sent)
                os.remove(rm_grammar)
        grammar_files.append(sentence)
        grammar_dict[sentence] = grammar_file
        self.extractor_lock.release()
        return grammar_file
        
    def translate(self, sentence, ctx_name=None):
        '''Decode a sentence (inc extracting a grammar if needed)
        Threadsafe, FIFO'''
        lock = self.ctx_locks[ctx_name]
        lock.acquire()
        self.lazy_ctx(ctx_name)
        # Empty in, empty out
        if sentence.strip() == '':
            lock.release()
            return ''
        if self.norm:
            sentence = self.tokenize(sentence)
            logging.info('Normalized input: {}'.format(sentence))
        grammar_file = self.grammar(sentence, ctx_name)
        decoder = self.decoders[ctx_name]
        start_time = time.time()
        hyp = decoder.decoder.decode(sentence, grammar_file)
        stop_time = time.time()
        logging.info('Translation time: {} seconds'.format(stop_time - start_time))
        # Empty reference: HPYPLM does not learn prior to next translation
        decoder.ref_fifo.write('\n')
        decoder.ref_fifo.flush()
        if self.norm:
            logging.info('Normalized translation: {}'.format(hyp))
            hyp = self.detokenize(hyp)
        lock.release()
        return hyp

    def tokenize(self, line):
        self.tokenizer_lock.acquire()
        self.tokenizer.stdin.write('{}\n'.format(line))
        tok_line = self.tokenizer.stdout.readline().strip()
        self.tokenizer_lock.release()
        return tok_line

    def detokenize(self, line):
        self.detokenizer_lock.acquire()
        self.detokenizer.stdin.write('{}\n'.format(line))
        detok_line = self.detokenizer.stdout.readline().strip()
        self.detokenizer_lock.release()
        return detok_line

    def command_line(self, line, ctx_name=None):
        # COMMAND [ctx_name] ||| arg1 [||| arg2 ...]
        args = [f.strip() for f in line.split('|||')]
        if args[-1] == '':
            args = args[:-1]
        if len(args) > 0:
            cmd_name = args[0].split()
            # ctx_name provided
            if len(cmd_name) == 2:
                (cmd_name, ctx_name) = cmd_name
            # ctx_name default/passed
            else:
                cmd_name = cmd_name[0]
            (command, nargs) = self.COMMANDS.get(cmd_name, (None, None))
            if command and len(args[1:]) in nargs:
                logging.info('{} ({}) ||| {}'.format(cmd_name, ctx_name, ' ||| '.join(args[1:])))
                return command(*args[1:], ctx_name=ctx_name)
        logging.info('ERROR: command: {}'.format(' ||| '.join(args)))
        
    def learn(self, source, target, ctx_name=None):
        '''Learn from training instance (inc extracting grammar if needed)
        Threadsafe, FIFO'''
        lock = self.ctx_locks[ctx_name]
        lock.acquire()
        self.lazy_ctx(ctx_name)
        if '' in (source.strip(), target.strip()):
            logging.info('ERROR: empty source or target: {} ||| {}'.format(source, target))
            lock.release()
            return
        if self.norm:
            source = self.tokenize(source)
            target = self.tokenize(target)
        # Align instance
        alignment = self.aligner.align(source, target)
        grammar_file = self.grammar(source, ctx_name)
        # MIRA update before adding data to grammar extractor
        decoder = self.decoders[ctx_name]
        mira_log = decoder.decoder.update(source, grammar_file, target)
        logging.info('MIRA: {}'.format(mira_log))
        # Add to HPYPLM by writing to fifo (read on next translation)
        logging.info('Adding to HPYPLM: {}'.format(target))
        decoder.ref_fifo.write('{}\n'.format(target))
        decoder.ref_fifo.flush()
        # Store incremental data for save/load
        self.ctx_data[ctx_name].append((source, target, alignment))
        # Add aligned sentence pair to grammar extractor
        logging.info('Adding to bitext: {} ||| {} ||| {}'.format(source, target, alignment))
        self.extractor.add_instance(source, target, alignment, ctx_name)
        # Clear (old) cached grammar
        rm_grammar = self.grammar_dict[ctx_name].pop(source)
        os.remove(rm_grammar)
        lock.release()

    def save_state(self, filename=None, ctx_name=None):
        lock = self.ctx_locks[ctx_name]
        lock.acquire()
        self.lazy_ctx(ctx_name)
        ctx_data = self.ctx_data[ctx_name]
        out = open(filename, 'w') if filename else sys.stdout
        logging.info('Saving state for context ({}) with {} sentences'.format(ctx_name, len(ctx_data)))
        out.write('{}\n'.format(self.decoders[ctx_name].decoder.get_weights()))
        for (source, target, alignment) in ctx_data:
            out.write('{} ||| {} ||| {}\n'.format(source, target, alignment))
        out.write('EOF\n')
        if filename:
            out.close()
        lock.release()

    def load_state(self, filename=None, ctx_name=None):
        lock = self.ctx_locks[ctx_name]
        lock.acquire() 
        self.lazy_ctx(ctx_name)
        ctx_data = self.ctx_data[ctx_name]
        decoder = self.decoders[ctx_name]
        input = open(filename) if filename else sys.stdin
        # Non-initial load error
        if ctx_data:
            logging.info('ERROR: Incremental data has already been added to context ({})'.format(ctx_name))
            logging.info('       State can only be loaded to a new context.')
            lock.release()
            return
        # Many things can go wrong if bad state data is given
        try:
            # MIRA weights
            line = input.readline().strip()
            # Throws exception if bad line
            decoder.decoder.set_weights(line)
            logging.info('Loading state...')
            start_time = time.time()
            # Lines source ||| target ||| alignment
            while True:
                line = input.readline()
                if not line:
                    raise Exception('End of file before EOF line')
                line = line.strip()
                if line == 'EOF':
                    break
                (source, target, alignment) = line.split(' ||| ')
                ctx_data.append((source, target, alignment))
                # Extractor
                self.extractor.add_instance(source, target, alignment, ctx_name)
                # HPYPLM
                hyp = decoder.decoder.decode(LIKELY_OOV)
                decoder.ref_fifo.write('{}\n'.format(target))
                decoder.ref_fifo.flush()
            stop_time = time.time()
            logging.info('Loaded state for context ({}) with {} sentences in {} seconds'.format(ctx_name, len(ctx_data), stop_time - start_time))
            lock.release()
        # Recover from bad load attempt by restarting context.
        # Guaranteed not to cause data loss since only a new context can load state.
        except:
            logging.info('ERROR: could not load state, restarting context ({})'.format(ctx_name))
            # ctx_name is already owned and needs to be restarted before other blocking threads use
            self.drop_ctx(ctx_name, force=True)
            self.lazy_ctx(ctx_name)
            lock.release()
