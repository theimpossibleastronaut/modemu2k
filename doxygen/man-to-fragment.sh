#!/bin/sh
# Render a man page to an HTML fragment suitable for doxygen's
# @htmlinclude. groff -Thtml -mandoc emits a full standalone HTML
# document; we strip the <html>/<head>/<body> wrapper so the fragment
# can be spliced into a doxygen page without nesting <html> inside
# <body>.
set -eu
groff -Thtml -mandoc "$1" | sed -n '/<body>/,/<\/body>/p' | sed '1d;$d'
