#!/usr/bin/perl -w

use strict;
use utf8;
use Getopt::Long;

my $weights_file;
my $hyp_file;
my $help;

Getopt::Long::Configure("no_auto_abbrev");
if (GetOptions(
    "weights_file|w=s" => \$weights_file,
    "hypothesis_file|h=s" => \$hyp_file,
    "help" => \$help,
) == 0 || @ARGV!=0 || $help || !$weights_file || !$hyp_file) {
  usage();
  exit(1);
}

open W, "<$weights_file" or die "Can't read $weights_file: $!";
my %weights;
while(<W>) {
  chomp;
  next if /^#/;
  next if /^\s*$/;
  my ($fname, $w) = split /\s+/;
  $weights{$fname} = $w;
}
close W;

my $cur = undef;
my %hyps = ();
open HYP, "<$hyp_file" or die "Can't read $hyp_file: $!";
while(<HYP>) {
  chomp;
  my ($id, $hyp, $feats) = split / \|\|\| /;
  unless (defined $cur) { $cur = $id; }
  if ($cur ne $id) {
    extract_1best(\%hyps);
    $cur = $id;
    %hyps = ();
  }
  my @afeats = split /\s+/, $feats;
  my $tot = 0;
  for my $featpair (@afeats) {
    my ($fname,$fval) = split /=/, $featpair;
    my $weight = $weights{$fname};
    die "Unweighted feature '$fname'" unless defined $weight;
    $tot += ($weight * $fval);
  }
  $hyps{$hyp} = $tot;
}
extract_1best(\%hyps) if defined $cur;
close HYP;

sub extract_1best {
  my $rh = shift;
  my %hyps = %$rh;
  my $best_score = undef;
  my $best_hyp = undef;
  for my $hyp (keys %hyps) {
    if (!defined $best_score || $hyps{$hyp} > $best_score) {
      $best_score = $hyps{$hyp};
      $best_hyp = $hyp;
    }
  }
  print "$best_hyp\n";
}

sub usage {
  print <<EOT;
Usage: $0 -w weights.txt -h hyp.nbest.txt
  Reranks n-best lists with new weights, extracting the new 1-best entries.
EOT
}

