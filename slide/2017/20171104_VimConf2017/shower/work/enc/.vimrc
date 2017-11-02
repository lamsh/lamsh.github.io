"" Charset, Line ending
set encoding=utf-8
set fileencodings =ucs-bom
set fileencodings+=iso-2022-jp
" set fileencodings+=utf-8,euc-jp,cp932
" set fileencodings+=euc-jp,utf-8,cp932
set fileencodings+=euc-jp,cp932,utf-8
set fileencodings+=utf-32,utf-16

set fileformats=unix,dos,mac

"" Statusline
set laststatus=2

set statusline =[%n]%<%f\ %m%r%h%w  " File name
set statusline+=%<%y%{'['.(&fenc!=''?&fenc:&enc).':'.&ff.']'}  " Encoding
" set statusline+=[%{mode()}]  " Mode
set statusline+=%{&bomb?'[bomb]':''}  " BOM
set statusline+=%{&eol?'':'[noeol]'}  " EOL
set statusline+=[%04B]  " Character code
set statusline+=%=\ %4l/%4L\|%3v\|%4P  " Current position information
