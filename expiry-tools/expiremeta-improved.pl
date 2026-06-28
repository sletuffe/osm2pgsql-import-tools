#!/usr/bin/perl

# Improved version of expiremeta.pl:
# Deduplicates metatile coordinates before touching files,
# avoiding redundant stat() calls for parent metatiles shared
# by many input tiles.

use strict;
use Tirex::Metatile;
use Tirex::Map;
use Tirex;
use File::Touch;
use Getopt::Long;

my $minz = 12;
my $timeoffset = 8000;
my $map = undef;
my $configdir = "/etc/tirex";
my $dryrun = 0;

GetOptions("map=s" => \$map, "minzoom=i" => \$minz, "config=s" => \$configdir, "dryrun" => \$dryrun);

Tirex::Config::init("$configdir/tirex.conf");
Tirex::Renderer->read_config_dir($configdir);

if (!defined($map))
{
    printf STDERR "usage: $0 --map=mapname [--minzoom=z] [--config=configdir]\n";
    exit(1);
}

my $limit = [];
for (my $i=0; $i<21; $i++) { $limit->[$i] = 2**$i-1 };
my $time = time() - $timeoffset * 86400;
my $touch = File::Touch->new(mtime_only => 1, time => $time, no_create => 1);
my $tiledir = Tirex::Map->get($map)->get_tiledir();

# Pass 1: accumulate all unique metatile coordinates across all zoom levels
my %seen;
my $reported = 0;

while(<STDIN>)
{
    my ($z, $x, $y) = split(/\//);
    if ($z < 0 or $z > 20 or $x < 0 or $y < 0 or $x > $limit->[$z] or $y > $limit->[$z])
    {
        die("invalid line on input: $_");
    }
    $reported++;

    # Walk up the zoom pyramid and register each unique metatile
    my ($mx, $my, $mz) = ($x<<3, $y<<3, $z+3);
    while ($mz >= $minz)
    {
        my $key = "$mz/$mx/$my";
        last if exists $seen{$key};  # already registered this metatile and all its parents
        $seen{$key} = [$mx, $my, $mz];
        $mz--; $mx = ($mx>>4)<<3; $my = ($my>>4)<<3;
    }
}

# Pass 2: touch each unique metatile once
my $touched = 0;
my $nonex = 0;
my $deduped = scalar keys %seen;

for my $coords (values %seen)
{
    my ($mx, $my, $mz) = @$coords;
    my $mt = Tirex::Metatile->new(map => $map, x=>$mx, y=>$my, z=>$mz);
    my $fullname = $tiledir . '/' . $mt->get_filename();
    if (-e $fullname) {
        $touch->touch($fullname) unless ($dryrun);
        $touched++;
    }
    else {
        $nonex++;
    }
}

printf("%d tiles reported, %d unique metatiles after dedup, %d did not exist, %d %s\n",
    $reported, $deduped, $nonex, $touched,
    $dryrun ? "would have been touched" : "touched");
