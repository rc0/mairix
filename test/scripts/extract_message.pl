#!/usr/bin/perl

# Extract mbox/MMDF messages, including .bz2/.gz/.xz, to help debugging
# mairix.  Expects command line to contain a mairix error message,
# including a filename and a [start,end) byte range; extracts those
# bytes from the (optionally compressed) mbox/MMDF file.
#
# Example:
#
#   extract_message.pl 'Message in foo.mmdf.xz at [5,2300) is misformatted'


use Modern::Perl '2018';
use autodie ':all';

my $args = join(' ', @ARGV);
my ($file, $suf, $from, $to) =
    ($args =~ /(\S+\.(?:mmdf|mbox)(?:\.(bz2|gz|xz))?).*\[(\d+),(\d+)\)/) or
    die "Expected to find \"path.(mmdf|mbox)[.bz2|.gz|.xz]...[NNN,MMM)\" in c",
    "ommand line.\n";
my %decompressors = (bz2 => 'bzcat', gz => 'zcat', xz => 'xzcat');

my $fh;
my $message;
if($suf) {
  open $fh, '-|', $decompressors{$suf}, $file;
  read $fh, $message, $from;
} else {
  open $fh, '<', $file;
  seek $fh, $from, 'SEEK_SET';
}
read $fh, $message, $to - $from;
say "--\n", $message, "--";
no autodie 'close'; # Closing a pipe before EOF causes unhappiness.
close $fh;
