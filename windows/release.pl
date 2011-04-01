#!/usr/local/bin/perl

print `make -f Makefile.cyg`;

my $version_no = `grep Noodle ../version.c`;
chomp $version_no;
$version_no =~ s/^.*?([0-9\.]+).*$/\1/;

print `rm "putty6.0_nd$version_no.zip"`;
print `zip -9 -v "putty6.0_nd$version_no.zip" *.exe release_note.txt`;
