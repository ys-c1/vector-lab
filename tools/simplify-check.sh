#!/bin/bash
# End-to-end check of the opt-in decimation export.
#
#   1. Each of the four path classes is decimated via the CLI and its dumped
#      points are re-verified independently (in awk): the kept points are an
#      ordered subsequence of the input, and the measured deviation is within
#      the bound.
#   2. Rejections: negative tolerance, and fewer than two points.
#   3. Legacy mode (no --simplify) reproduces the frozen full-export bytes.
#
# Scene polygons are implicitly closed, so the CLI treats every path as a
# ring.  The library-level bench (render/build/bench) covers open paths.
#
# Usage: ./tools/simplify-check.sh [path-to-cli]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI=${1:-render/build/cli}
case "$CLI" in /*) ;; *) CLI=$(cd "$(dirname "$CLI")" && pwd)/$(basename "$CLI") ;; esac
FIX="$ROOT/render/fixtures"
TMP=${TMPDIR:-/tmp}
TMP=$(mktemp -d "${TMP}/simplify-check.XXXXXX" 2>/dev/null) \
  || TMP=$(mktemp -d "${ROOT}/render/build/simplify-check.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
fail=0

# Counts points (x y pairs) in a scene file, skipping the "p" token.
count_pts() {
  awk '{ for (i=1;i<=NF;i++) if ($i!="p") n++ } END { print n/2+0 }' "$1"
}

# Independent re-measurement of max deviation and order preservation.
# Prints "<ordered 0|1> <maxdev>".  Shares no code with the C implementation.
measure() { # $1=original $2=kept $3=closed(0/1)
awk -v closed="$3" '
  function d2(x1,y1,x2,y2){return (x2-x1)^2+(y2-y1)^2}
  function segdist(px,py,ax,ay,bx,by,   dx,dy,l2,t,qx,qy){
    dx=bx-ax; dy=by-ay; l2=dx*dx+dy*dy
    if (l2<=0) return sqrt(d2(px,py,ax,ay))
    t=((px-ax)*dx+(py-ay)*dy)/l2
    if (t<0) t=0; if (t>1) t=1
    qx=ax+dx*t; qy=ay+dy*t
    return sqrt(d2(px,py,qx,qy))
  }
  FNR==1 { file++ }
  {
    pending = 0
    for (i=1;i<=NF;i++) {
      if ($i=="p") { pending=0; continue }
      if (!pending) { vx=$i+0; pending=1 }
      else {
        vy=$i+0; pending=0
        if (file==1) { nx++; ox[nx]=vx; oy[nx]=vy }
        else         { kx++; sx[kx]=vx; sy[kx]=vy }
      }
    }
  }
  END {
    j=1
    for (i=1;i<=nx && j<=kx;i++)
      if (ox[i]==sx[j] && oy[i]==sy[j]) j++
    ordered = (j==kx+1) ? 1 : 0
    segs = closed ? kx : kx-1
    worst=0
    for (i=1;i<=nx;i++) {
      best=-1
      for (m=1;m<=segs;m++) {
        k = (m<kx) ? m+1 : 1
        d = segdist(ox[i],oy[i],sx[m],sy[m],sx[k],sy[k])
        if (best<0 || d<best) best=d
      }
      if (best>worst) worst=best
    }
    printf "%d %.9f\n", ordered, worst
  }
' "$1" "$2"
}

check_shape() { # name file closed bound
  local name=$1 file=$2 closed=$3 bound=$4
  local out="$TMP/$name.pgm" dump="$TMP/$name.pts.txt"
  if ! "$CLI" "$file" "$out" 40 40 1.0 --simplify "$bound" --dump "$dump" >/dev/null 2>"$TMP/$name.err"; then
    echo "$name: CLI failed: $(cat "$TMP/$name.err")"; fail=1; return
  fi
  local in kept ordered dev
  in=$(count_pts "$file")
  kept=$(count_pts "$dump")
  read -r ordered dev < <(measure "$file" "$dump" "$closed")
  local verdict=ok
  awk -v d="$dev" -v b="$bound" -v o="$ordered" 'BEGIN{exit !(d<=b+1e-9 && o==1)}' || { verdict=OVER; fail=1; }
  if [ "$closed" = 1 ] && [ "$kept" -lt 3 ]; then verdict="RING<3"; fail=1; fi
  printf "%-6s in %4d kept %4d (%5.1f%%) maxdev %.6f bound %.2f order %s  %s\n" \
         "$name" "$in" "$kept" "$(awk -v k="$kept" -v n="$in" 'BEGIN{print (n?100*k/n:0)}')" \
         "$dev" "$bound" "$([ "$ordered" = 1 ] && echo kept || echo LOST)" "$verdict"
}

echo "decimation (bound 0.5), re-measured independently:"
check_shape line "$FIX"/shape_line.txt 1 0.5
check_shape arc  "$FIX"/shape_arc.txt  1 0.5
check_shape saw  "$FIX"/shape_saw.txt  1 0.5
check_shape ring "$FIX"/shape_ring.txt 1 0.5

echo "rejections:"
"$CLI" "$FIX"/shape_ring.txt "$TMP/x.pgm" 16 16 1 --simplify -1 >/dev/null 2>"$TMP/e1"
rc=$?; msg=$(cat "$TMP/e1")
if [ $rc -ne 0 ] && [ -n "$msg" ]; then echo "  negative tolerance: rc $rc ($msg)"; else echo "  negative tolerance: NOT REJECTED"; fail=1; fi
printf 'p 0 0\n' >"$TMP/one.txt"
"$CLI" "$TMP/one.txt" "$TMP/x.pgm" 16 16 1 --simplify 0.5 >/dev/null 2>"$TMP/e2"
rc=$?; msg=$(cat "$TMP/e2")
if [ $rc -ne 0 ] && [ -n "$msg" ]; then echo "  one point: rc $rc ($msg)"; else echo "  one point: NOT REJECTED"; fail=1; fi

echo "legacy export (new logic off) byte-identical:"
for c in "scene_basic 16 16 1.0" "scene_tri 40 30 1.3" "scene_collinear 24 24 2.0"; do
  set -- $c
  "$CLI" "$FIX/$1.txt" "$TMP/$1.pgm" "$2" "$3" "$4" >/dev/null || fail=1
  if cmp -s "$TMP/$1.pgm" "$FIX/$1.pgm"; then
    echo "  $1: IDENTICAL"
  else
    echo "  $1: DIFF"; fail=1
  fi
done

[ $fail -eq 0 ] && echo "simplify-check: PASS" || echo "simplify-check: FAIL"
exit $fail
