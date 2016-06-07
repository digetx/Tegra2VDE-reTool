#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use File::Slurp;

my $fpath = $ARGV[0];
my $split_path = dirname($fpath);
my $filename = basename($fpath);

my $text = read_file($fpath);
my $fi = 0;

mkdir "$split_path/split_$filename";

while ($text =~ /CLK_RST_CONTROLLER_RST_DEV_H_SET_0"(.+?0x00000001\t"INT_VDE_SXE")/msg) {
	my $z = $1;
	$z =~ s/.*CLK_RST_CONTROLLER_RST_DEV_H_SET_0"//msg;
	$z =~ s/ON_AVP: WRITE32:  0x6001B08C 0x00000001	"BSEV Unknown".*//msg;

	write_file("$split_path/split_$filename/" . $fi++, $z);
}

$fi = 0;

while ($text =~ /[\+]{46}(.+?)[-]{46}/msg) {
	write_file("$split_path/split_$filename/d" . $fi++, $1);
}
