#!/bin/sh
# fixdox.sh - This script will fix up the texi file for proper
#             generation of the final document.
#
# input is stdin and output is stdout.
#
# written by Marco van den Heuvel <blackystardust68@yahoo.com>
#
# fixdox.sh <format-to-fix-for>
#           $1

FORMAT=$1

table4chm()
{
  rm -f chmtmp*.txt
  read data
  newdata=""
  for i in $data
  do
    if test x"$i" != "x@item"; then
      newdata="$newdata $i"
    fi
  done
  echo >>chmtmp.txt "@multitable $newdata"
  read data
  while test x"$data" != "x@end multitable"
  do
    echo >>chmtmp.txt "$data"
    read data
  done
  sed 's/@item/@rowstart/g' <chmtmp.txt >chmtmp2.txt
  cat chmtmp2.txt
  rm -f chmtmp*.txt
}

fixtxt()
{
  while read data
  do
    if test x"$data" != "xSTART-INFO-DIR-ENTRY" -a x"$data" != "xEND-INFO-DIR-ENTRY"; then
      echo "$data"
    else
      if test x"$data" = "xSTART-INFO-DIR-ENTRY"; then
        read data
        header=""
        for i in $data
        do
          if test x"$header" != "x"; then
             header="$header $i"
          fi
          if test x"$header" = "x" -a x"$i" = "x(vice)."; then
             header=" "
          fi
        done
        echo $header
      fi
    fi
  done
}

fixchm()
{
  outputok=yes
  while read data
  do
    case x"${data}" in
	"x@multitable"*)  table4chm;;
    esac

    if test x"$data" = "x@ifinfo"; then
      outputok=no
    fi

    if test x"$outputok" = "xyes"; then
      echo $data
    fi

    if test x"$data" = "x@end ifinfo"; then
      outputok=yes
    fi
  done
}

fixhlp()
{
  echo not implemented yet
}

fixguide()
{
  echo not implemented yet
}

fixpdf()
{
  echo not implemented yet
}

fixipf()
{
  echo not implemented yet
}

if test x"$FORMAT" = "xtxt"; then
  fixtxt
fi

if test x"$FORMAT" = "xchm"; then
  fixchm
fi

if test x"$FORMAT" = "xhlp"; then
  fixhlp
fi

if test x"$FORMAT" = "xguide"; then
  fixguide
fi

if test x"$FORMAT" = "xpdf"; then
  fixpdf
fi

if test x"$FORMAT" = "xipf"; then
  fixipf
fi
