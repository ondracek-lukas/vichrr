#!/usr/bin/perl

use v5.14;
# use Data::Dumper;
say STDERR "Generating lang.gen.hpp...";

my %data; # {os}{lang}{name} = string

#my %langs;

sub appendStr {
	my ($l, $name, $os, $str) = @_;
	$str =~ s/\s*$//;
	for $os ($os || ("mac", "linux", "win", "other")) {
		$data{$os} //= {};
		$data{$os}{$l} //= {};
		if (defined $data{$os}{$l}{$name}) {
			$data{$os}{$l}{$name} .= "\n$str";
		} else {
			$data{$os}{$l}{$name} = $str;
		}
	}
}

while ($_ = shift @ARGV) {
	open my $file, "<", $_ or die "Cannot open file.";
	my $l = s=^.*[\\/](.*).txt=$1=gr;

	#my %lang;
	my $name = "";
	my $os = "";
	my $str = "";
	while (<$file>) {
		if (/^# (.*)/) {
			my $newName = $1;
			appendStr($l, $name, $os, $str) if $name;
			#$lang{$name} = $str =~ s/\s*$//r if $name;
			if ($newName =~ /(.*):(.*)/) {
				$name = $1;
				$os = $2;
			} else {
				$name = $newName;
				$os = "";
			}
			$str = "";
		} else {
			$str .= $_;
		}
	}
	appendStr($l, $name, $os, $str) if $name;
	#$lang{$name} = $str =~ s/\s*$//r if $name;
	#$langs{$l} = \%lang;
	close $file;
}



print "#include <wx/string.h>\n";

my %ifs = (
	win => "#ifdef __WIN32__",
	mac => "#elif __APPLE__",
	linux => "#elif __linux__",
	other => "#else");

for my $os ("win", "mac", "linux", "other") {
	my @keys = sort keys %{$data{$os}{"en"}};

	print "$ifs{$os}\n";

	print "enum LANG_STR_NAME {";
	print "$_, " for @keys;
	print "};\n";

	for my $l (sort keys %{$data{$os}}) {
		print "const char *LANG_" . uc($l) . "[] = {\n";
		for my $n (@keys) {
			my $str = $data{$os}{$l}{$n};
			unless ($str) {
				$str = $data{$os}{"en"}{$n};
				say STDERR "Missing $l string for $n:$os.";
			}
			print qq{\tR"-SEP-($str)-SEP-",\n};
		}
		print "};\n";
	}
}
print "#endif";

print q(
const char **LANG = LANG_EN;
void langInit(wxString name) {
);
print qq{\tif (name.StartsWith("$_")) LANG = LANG_} . uc($_) . ";\n" for sort keys %{%data{"linux"}};
print "}\n";

print "#define STR(name) wxString::FromUTF8(LANG[LANG_STR_NAME::name])";
