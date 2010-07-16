#!/usr/bin/perl -w
#hooks up two processes, 2nd of which has one line of output per line of input, expected by the first, which starts off the communication

# if you don't know how to fork/exec in a C program, this could be helpful under limited cirmustances (would be ok to liaise with sentserver)

#WARNING: because it waits for the result from command 2 after sending every line, and especially if command 1 does the same, using sentserver as command 2 won't actually buy you any real parallelism.

use strict;
use IPC::Open2;
use POSIX qw(pipe dup2 STDIN_FILENO STDOUT_FILENO);
#use IO::Handle;
$,=' ';
my $quiet=$ENV{QUIET};
sub info {
    print STDERR @_ unless $quiet;
}

my @c1;
if (scalar @ARGV) {
    do {
        push @c1,shift
    } while scalar @ARGV && $c1[$#c1] ne '--';
}
pop @c1;
my @c2=@ARGV;
@ARGV=();
(scalar @c1 && scalar @c2) || die "usage: $0 cmd1 args -- cmd2 args; hooks up two processes, 2nd of which has one line of output per line of input, expected by the first, which starts off the communication.  crosses stdin/stderr of cmd1 and cmd2 line by line (both must flush on newline and output.  cmd1 initiates the conversation (sends the first line).  QUIET=1 env var suppresses debugging output.  default: attempts to cross stdin/stdout of c1 and c2 directly (via two unidirectional posix pipes created before fork).  env SERIAL=1: (no parallelism possible) but lines exchanged are logged unless QUIET.  if SNAKE then stdin -> c1 -> c2 -> c1 -> stdout";

info("1 cmd:",@c1,"\n");
info("2 cmd:",@c2,"\n");

sub lineto {
    select $_[0];
    $|=1;
    shift;
    print @_;
}
my $snake=$ENV{SNAKE};
my $serial=$ENV{SERIAL};
if ($serial || $snake) {
    my ($R1,$W1,$R2,$W2);
    my $c1p=open2($R1,$W1,@c1); # Open2 R W backward from Open3.
    my $c2p=open2($R2,$W2,@c2);
    if ($snake) {
        while(<STDIN>) {
            lineto($W1,$_);
            last unless defined ($_=<$R1>);
            lineto($W2,$_);
            last unless defined ($_=<$R2>);
            lineto($W1,$_);
            last unless defined ($_=<$R1>);
            lineto(*STDOUT,$_);
        }
    } else {
        while(<$R1>) {
            info("1:",$_);
            lineto($W2,$_);
            last unless defined ($_=<$R2>);
            info("2:",$_);
            lineto($W1,$_);
        }
    }
} else {
    my @rw1=POSIX::pipe();
    my @rw2=POSIX::pipe();
    my $pid=undef;
    $SIG{CHLD} = sub { wait };
    while (not defined ($pid=fork())) {
        sleep 1;
    }
    POSIX::close(STDOUT_FILENO);
    POSIX::close(STDIN_FILENO);
    if ($pid) {
        POSIX::dup2($rw1[1],STDOUT_FILENO);
        POSIX::dup2($rw2[0],STDIN_FILENO);
        exec @c1;
    } else {
        POSIX::dup2($rw2[1],STDOUT_FILENO);
        POSIX::dup2($rw1[0],STDIN_FILENO);
        exec @c2;
    }
    while (wait()!=-1) {}
}
