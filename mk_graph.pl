#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use File::Slurp;
use GraphViz2;

my @colors = (
	'blue',
	'red',
	'green',
	'orange',
	'turquoise',
	'sienna',
	'chocolate',
	'burlywood',
	'lightslategray',
	'firebrick',
	'deeppink',
	'cyan',
	'gold',
	'greenyellow',
	'indianred',
	'limegreen',
	'indigo',
);
my $colitr = 0;

my $graph = GraphViz2->new();
my $gpath = defined($ARGV[1]) ? $ARGV[1] : 'graph.png';

my $fpath = $ARGV[0];
my $text = read_file($fpath);
my $fn = 0;

my %ptags;

while ($text =~ /CLK_RST_CONTROLLER_RST_DEV_H_SET_0"(.+?0x00000001\t"INT_VDE_SXE")/msg) {
	my $z = $1;
	$z =~ s/.*CLK_RST_CONTROLLER_RST_DEV_H_SET_0"//msg;
	$z =~ s/ON_AVP: WRITE32:  0x6001B08C 0x00000001	"BSEV Unknown".*//msg;

	my $pfn = $fn - 1;
	my @frame_ids;
	my %tags;

	push @frame_ids, { text => "{FRAME #$fn" };

	foreach my $FID (0 .. 16) {
		my $fidaddr = sprintf "0x%08X", 0x6001D800 + $FID * 4;
		my $nfidaddr = sprintf "0x%08X", 0x6001D800 + ($FID + 1) * 4;

		if ($z =~ /$fidaddr (0x[0-9A-F]{8})/) {
			my $mem = $1;
			my $mfn = $ptags{"fn$mem"};
			my $rframe = defined($mfn) ? " FRAME #$mfn" : "";

			push @frame_ids, { text => "FRAMEID $FID -$rframe $mem",
					   port => "<fp$FID>" };
			$tags{$mem} = $FID;
			$tags{"fn$mem"} = $fn if ($FID == 0);

			if ($FID == 0 && $z !~ /$nfidaddr 0x/) {
				%ptags = ();
				last;
			}

			my $p_port = $ptags{$mem};

			if ($FID != 0 && defined($p_port)) {
				$graph->add_edge(from => "Frame_$pfn:fp$p_port",
						 to => "Frame_$fn:fp$FID",
						 color => $colors[$colitr++ % scalar(@colors)]);
			}
		}
	}

	while ($z =~ /0x4000(....) (0x[0-9A-F]{8})\t"IRAM"\n.+?0x4000.... (0x[0-9A-F]{8})/g) {
		my $from = $ptags{$3};

		push @frame_ids, { text => "IRAM 0x$1: $2 - " . (defined($from) ? $from : "??? $3"),
				   port => "<iaddr$1>" };

		next if (hex($1) >= 0x4070);

		if (defined($from)) {
			$graph->add_edge(from => $from,
					 to => "Frame_$fn:iaddr$1",
					 style => 'dashed',
					 color => $colors[$colitr++ % scalar(@colors)]);
		}
	}

	my ($MBE_out_enb) = ($z =~ /0x6001C080 0xFC0000([0-9A-F]{2})/);
	die("No 0xFC0000xx!!! Frame #$fn\n") if (!defined($MBE_out_enb));

	foreach my $MBE_reg (0 .. 4) {
		my $rlow = $MBE_reg * 2;
		my $rhigh = $rlow + 1;

		my ($low) = ($z =~ /0x6001C080 0xA$rlow.0([0-9A-F]{4})/);
		my ($high) = ($z =~ /0x6001C080 0xA$rhigh.0([0-9A-F]{4})/);

		next if (!defined($low) && !defined($high));

		my $maddr = "0x$high$low";

		push @frame_ids, { text => "MBE 0xA$rlow-0xA$rhigh $maddr", port => "<mb${maddr}>" };

		if (hex("0x$MBE_out_enb") & (($MBE_reg == 2) || ($MBE_reg == 4 ? 2 : 0))) {
			$tags{$maddr} = "Frame_$fn:mb${maddr}";
		}
	}

	while ($z =~ /0x6001C080 (0xD[0-9A-F]{7})/g) {
		push @frame_ids, { text => "$1" };
	}

	if ($z =~ /0x6001C080 (0x3[0-9A-F]{7})/) {
		push @frame_ids, { text => $1 };
	}

	push @frame_ids, { text => "0xFC0000$MBE_out_enb" };

	$frame_ids[-1]{text} = $frame_ids[-1]{text} . '}';

	$graph->add_node(name => "Frame_$fn",
			 label => \@frame_ids,
			 shape => 'record');

	%ptags = (%ptags, %tags);

	$fn++;
}

$graph->run(format => 'png', output_file => $gpath);
