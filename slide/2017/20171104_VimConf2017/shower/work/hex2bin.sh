:
################################################################################
## \file      hex2bin.sh
## \author    SENOO, Ken
## \copyright CC0
################################################################################

hex2bin(){
	if printf '%s' ${1+"$@"} | grep -qv '^[0-9A-Fa-f]*$'; then
		echo 'Invalid hexadecimal' \'${1+"$@"}\' >&2
		return 1
	fi

	for arg; do
		printf \\$(printf "%o" $((0x$arg)) )
	done
}

hex2bin ${1+"$@"}
# 入口
A
