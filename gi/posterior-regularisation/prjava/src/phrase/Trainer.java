package phrase;

import io.FileUtil;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.util.Random;

import arr.F;

public class Trainer 
{
	public static void main(String[] args) 
	{
        OptionParser parser = new OptionParser();
        parser.accepts("help");
        parser.accepts("in").withRequiredArg().ofType(File.class);
        parser.accepts("out").withRequiredArg().ofType(File.class);
        parser.accepts("parameters").withRequiredArg().ofType(File.class);
        parser.accepts("topics").withRequiredArg().ofType(Integer.class).defaultsTo(5);
        parser.accepts("em-iterations").withRequiredArg().ofType(Integer.class).defaultsTo(5);
        parser.accepts("pr-iterations").withRequiredArg().ofType(Integer.class).defaultsTo(0);
        parser.accepts("threads").withRequiredArg().ofType(Integer.class).defaultsTo(0);
        parser.accepts("scale-phrase").withRequiredArg().ofType(Double.class).defaultsTo(5.0);
        parser.accepts("scale-context").withRequiredArg().ofType(Double.class).defaultsTo(0.0);
        parser.accepts("seed").withRequiredArg().ofType(Long.class).defaultsTo(0l);
        parser.accepts("convergence-threshold").withRequiredArg().ofType(Double.class).defaultsTo(1e-6);
        OptionSet options = parser.parse(args);

        if (options.has("help") || !options.has("in"))
        {
        	try {
				parser.printHelpOn(System.err);
			} catch (IOException e) {
				System.err.println("This should never happen. Really.");
				e.printStackTrace();
			}
        	System.exit(1);     
        }
		
		int tags = (Integer) options.valueOf("topics");
		int em_iterations = (Integer) options.valueOf("em-iterations");
		int pr_iterations = (Integer) options.valueOf("pr-iterations");
		double scale_phrase = (Double) options.valueOf("scale-phrase");
		double scale_context = (Double) options.valueOf("scale-context");
		int threads = (Integer) options.valueOf("threads");
		double threshold = (Double) options.valueOf("convergence-threshold");
		
		if (options.has("seed"))
			F.rng = new Random((Long) options.valueOf("seed"));
		
		if (tags <= 1 || scale_phrase < 0 || scale_context < 0 || threshold < 0)
		{
			System.err.println("Invalid arguments. Try again!");
			System.exit(1);
		}
		
		Corpus corpus = null;
		File infile = (File) options.valueOf("in");
		try {
			System.out.println("Reading concordance from " + infile);
			corpus = Corpus.readFromFile(FileUtil.reader(infile));
			corpus.printStats(System.out);
		} catch (IOException e) {
			System.err.println("Failed to open input file: " + infile);
			e.printStackTrace();
			System.exit(1);
		}
		
 		System.out.println("Running with " + tags + " tags " +
 				"for " + em_iterations + " EM and " + pr_iterations + " PR iterations " +
 				"with scale " + scale_phrase + " phrase and " + scale_context + " context " +
 				"and " + threads + " threads");
 		System.out.println();
		
		PhraseCluster cluster = new PhraseCluster(tags, corpus, scale_phrase, scale_context, threads);
				
		double last = 0;
		for (int i=0; i<em_iterations+pr_iterations; i++)
		{
			double o;
			if (i < em_iterations) 
				o = cluster.EM();
			else if (scale_context == 0)
			{
				if (threads >= 1)
					o = cluster.PREM_phrase_constraints_parallel();
				else
					o = cluster.PREM_phrase_constraints();
			}
			else 
				o = cluster.PREM_phrase_context_constraints();
			
			System.out.println("ITER: "+i+" objective: " + o);
			
			if (i != 0 && Math.abs((o - last) / o) < threshold)
			{
				last = o;
				if (i < em_iterations)
				{
					i = em_iterations - 1;
					continue;
				}
				else
					break;
			}
			last = o;
		}
		
		double pl1lmax = cluster.phrase_l1lmax();
		double cl1lmax = cluster.context_l1lmax();
		System.out.println("\nFinal posterior phrase l1lmax " + pl1lmax + " context l1lmax " + cl1lmax);
		if (pr_iterations == 0) 
			System.out.println("With PR objective " + (last - scale_phrase*pl1lmax - scale_context*cl1lmax));
		
		if (options.has("out"))
		{
			File outfile = (File) options.valueOf("out");
			try {
				PrintStream ps = FileUtil.printstream(outfile);
				cluster.displayPosterior(ps);
				ps.close();
			} catch (IOException e) {
				System.err.println("Failed to open output file: " + outfile);
				e.printStackTrace();
				System.exit(1);
			}
		}

		if (options.has("parameters"))
		{
			File outfile = (File) options.valueOf("parameters");
			PrintStream ps;
			try {
				ps = FileUtil.printstream(outfile);
				cluster.displayModelParam(ps);
				ps.close();
			} catch (IOException e) {
				System.err.println("Failed to open output parameters file: " + outfile);
				e.printStackTrace();
				System.exit(1);
			}
		}
		
		if (cluster.pool != null)
			cluster.pool.shutdown();
	}
}
