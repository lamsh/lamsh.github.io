#!/bin/bash
# \file      merge.sh
# \author    SENOO, Ken
# \copyright CC0
# \date      first created date: 2016-04-03T18:00+09:00
# \date      last  updated date: 2016-05-01T18:27+09:00

set -u

## Reveal.jsで閲覧のために最終版を作るのに必要な部分をマージする。
## スライドの本体はsrc.htmlに記述する。

cp src.html index.html

## CSS部分
### head要素の終了タグ </head> の直前に以下の文字列を挿入

CSS=$(cat << EOS
		<meta name="apple-mobile-web-app-capable" content="yes">
		<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">

		<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, minimal-ui">

		<link rel="stylesheet" href="css/reveal.css">
		<link rel="stylesheet" href="css/theme/black.css" id="theme">

		<!-- Code syntax highlighting -->
		<link rel="stylesheet" href="lib/css/zenburn.css">

		<!-- Printing and PDF exports -->
    <script>
			var link = document.createElement( 'link' );
			link.rel = 'stylesheet';
			link.type = 'text/css';
			link.href = window.location.search.match( /print-pdf/gi ) ? 'css/print/pdf.css' : 'css/print/paper.css';
			document.getElementsByTagName( 'head' )[0].appendChild( link );
		</script>
    <!-- <link ref=stylesheet href="css/print/pdf.css" /> -->

		<!--[if lt IE 9]>
		<script src="lib/js/html5shiv.js"></script>
		<![endif]-->
EOS
)

CSS=$(echo "$CSS" | tr "\n" '\\')
CSS=$(echo "${CSS//\\/\\n}")

sed -i "s@</head>@${CSS}</head>@" index.html

## 末尾のscript
### body 要素の終了タグ </body> の直前に以下を挿入

SCRIPT=$(cat << "EOS"
		<script src="lib/js/head.min.js"></script>
		<script src="js/reveal.js"></script>

		<script>

			// Full list of configuration options available at:
			// https://github.com/hakimel/reveal.js#configuration
			Reveal.initialize({
				controls: true,
				progress: true,
				history: true,
				center: true,

				transition: 'slide', // none/fade/slide/convex/concave/zoom

				// Optional reveal.js plugins
				dependencies: [
					{ src: 'lib/js/classList.js', condition: function() { return !document.body.classList; } },
				]
			});

        // My configuration
      Reveal.configure({ slideNumber: "c/t"});
		</script>
EOS
)

SCRIPT=$(echo "$SCRIPT" | tr "\n" '\\')
SCRIPT=$(echo "${SCRIPT//\\/\\n}")
sed -i "s@</body>@${SCRIPT}</body>@" index.html
