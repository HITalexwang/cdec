#!/usr/bin/perl -w
#hooks up two processes, 2nd of which has one line of output per line of input, expected by the first, which starts off the communication

# if you don't know how to fork/exec in a C program, this could be helpful under limited cirmustances (would be ok to liaise with sentserver)

#WARNING: because it waits for the result from command 2 after sending every line, and especially if command 1 does the same, using sentserver as command 2 won't actually buy you any real parallelism.

use strict;
use IPC::Open2;
$,=' ';
my $quiet=$ENV{QUIET};
sub info {
    print STDERR @_ unless $quiet;
}

my @c1;
do {
    push @c1,shift
} while $c1[$#c1] ne '--';
pop @c1;
my @c2=@ARGV;
(scalar @c1 && scalar @c2) || die "usage: $0 cmd1 args -- cmd2 args; hooks up two processes, 2nd of which has one line of output per line of input, expected by the first, which starts off the communication.  crosses stdin/stderr of cmd1 and cmd2 line by line (both must flush on newline and output.  cmd1 initiates the conversation (sends the first line).  QUIET env var suppresses debugging output.";
info("1 cmd:",@c1,"\n");
info("2 cmd:",@c2,"\n");
my ($R1,$W1,$R2,$W2);
my $c1p=open2($R1,$W1,@c1); # Open2 R W backward from Open3.
my $c2p=open2($R2,$W2,@c2);
select $W2;
$|=1;
while(<$R1>) {
    info("1:",$_);
    print $_;
    $_=<$R2>;
    last unless defined $_;
    info("2:",$_);
    print $W1 $_;
}
