#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<EOF
Usage: ./build_cli.sh [VARIANT_DIR] [options]

Hardware build of the lab3 cnn project for the Ultra96v2, with optional deploy
and timed runs on the board. Sources come from this workspace:

  18643_cnn_src/krnl_cnn.cpp                                     kernel
  cnn_host/cnn.cpp cnn.h util643.h kernel643.h instance643.h     host + shared headers

VARIANT_DIR, if given, overrides any of those files by name -- normally just
krnl_cnn.cpp and/or kernel643.h, the two the handout lets you change. All
sources are staged into one directory, so the kernel and the host always
compile against the same headers.

HLS settings come from krnl_cnn/hls_config.cfg and link settings from
cnn/hw_link/cnn-link.cfg, so clock changes made in the Vitis IDE apply here too.

Options:
  --name NAME        Build/deploy name (default: VARIANT_DIR's name, or "cnn")
  --hls-clock NS     HLS clock target in ns (default: hls_config.cfg)
  --pl-clock ID      Kernel clock: 0=150 1=300 2=75 3=100 4=200 5=400 6=600 MHz
                     (default: cnn-link.cfg, which means PL0 = 150 MHz)
  --host-only        Rebuild only the ARM host; reuse build/NAME's xclbin
  --clean            Delete build/NAME before building
  --deploy           Copy host, xclbin and BOOT.BIN to the board
  --no-boot          With --deploy, never copy BOOT.BIN (board keeps its design)
  --deploy-only      Skip the build and deploy build/NAME as last built
  --run              Deploy, reboot if the bitstream changed, run and log timings
  --runs N           Number of timed runs with --run (default: 3)
  --target TARGET    SSH target (default: root@nobug.wifi.local.cmu.edu)
  --remote-dir DIR   Board directory (default: /run/media/mmcblk0p1)
  -h, --help         Show this help

Examples:
  ./build_cli.sh --name starter                      # unmodified starter
  ./build_cli.sh --name starter --run                # ...then deploy and time it
  ./build_cli.sh experiments/tile64 --run            # variant with its own kernel643.h
  ./build_cli.sh --name starter_200 --pl-clock 4 --hls-clock 5
  ./build_cli.sh experiments/tile64 --host-only --run
  ./build_cli.sh --name starter --deploy-only --run  # no build: deploy and time it

A full build (HLS, Vivado place-and-route, packaging) takes 1-2 hours and
writes a ~3.4 GB SD card image under build/NAME/. Run it inside tmux or nohup.
EOF
}

die()  { printf '\e[0;31mError:\e[0m %s\n' "$*" >&2; exit 1; }
warn() { printf '\e[0;33mwarning:\e[0m %s\n' "$*" >&2; }
step() { printf '\n\e[1m[%s] %s\e[0m  (%s)\n' "$1" "$2" "$(date +%T)"; }

need_value() {
    if [[ $# -lt 2 ]]; then
        printf 'Missing value after %s\n' "$1" >&2
        usage
        exit 2
    fi
}

variant_arg=
name=
hls_clock=
pl_clock=
host_only=false
clean=false
deploy=false
copy_boot=true
deploy_only=false
run=false
runs=3
ssh_target=${FPGA_SSH_TARGET:-root@nobug.wifi.local.cmu.edu}
remote_dir=${FPGA_DEPLOY_DIR:-/run/media/mmcblk0p1}

while [[ $# -gt 0 ]]; do
    case $1 in
        --name)       need_value "$@"; name=$2; shift 2 ;;
        --hls-clock)  need_value "$@"; hls_clock=$2; shift 2 ;;
        --pl-clock)   need_value "$@"; pl_clock=$2; shift 2 ;;
        --host-only)  host_only=true; shift ;;
        --clean)      clean=true; shift ;;
        --deploy)     deploy=true; shift ;;
        --no-boot)    copy_boot=false; shift ;;
        --deploy-only) deploy_only=true; deploy=true; shift ;;
        --run)        run=true; deploy=true; shift ;;
        --runs)       need_value "$@"; runs=$2; shift 2 ;;
        --target)     need_value "$@"; ssh_target=$2; shift 2 ;;
        --remote-dir) need_value "$@"; remote_dir=$2; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        -*)
            printf 'Unknown option: %s\n' "$1" >&2
            usage
            exit 2
            ;;
        *)
            [[ -z $variant_arg ]] || die "only one VARIANT_DIR may be given"
            variant_arg=$1
            shift
            ;;
    esac
done

[[ -z $hls_clock || $hls_clock =~ ^[0-9]+(\.[0-9]+)?$ ]] || die "--hls-clock takes a period in ns, e.g. 5 or 6.6"
[[ -z $pl_clock || $pl_clock =~ ^[0-6]$ ]] || die "--pl-clock takes an ID from 0 to 6"
[[ $runs =~ ^[1-9][0-9]*$ ]] || die "--runs takes a positive integer"
[[ $remote_dir =~ ^/[A-Za-z0-9._/-]+$ ]] || die "invalid remote directory: $remote_dir"
if [[ $clean == true && $host_only == true ]]; then
    die "--clean would delete the xclbin that --host-only reuses"
fi
if [[ $deploy_only == true ]] &&
        [[ $host_only == true || $clean == true || -n $hls_clock || -n $pl_clock ]]; then
    die "--deploy-only skips the build, so --host-only, --clean, --hls-clock and --pl-clock do not apply"
fi

ws=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
host_files=(cnn.cpp cnn.h util643.h kernel643.h instance643.h)
kernel_files=(krnl_cnn.cpp util643.h kernel643.h instance643.h)

variant_dir=
if [[ -n $variant_arg ]]; then
    for candidate in "$variant_arg" "$ws/$variant_arg"; do
        if [[ -d $candidate ]]; then
            variant_dir=$(realpath -- "$candidate")
            break
        fi
    done
    [[ -n $variant_dir ]] || die "no such variant directory: $variant_arg"
    name=${name:-$(basename -- "$variant_dir")}
fi
name=${name:-cnn}
# Must start with a letter or digit: NAME becomes build/NAME, which --clean deletes.
[[ $name =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "invalid name '$name': use letters, digits, '.', '_', '-'"
[[ $name != deploy ]] || die "'deploy' is reserved for build/deploy/"

out=$ws/build/$name
deploy_dir=$ws/build/deploy
boot_bin=$out/package/out/sd_card/BOOT.BIN

build() {
    # --- Vitis 2023.2 environment ---------------------------------------------
    vpp_version=$(v++ --version 2>&1 || true)
    if [[ $vpp_version != *v2023.2* ]]; then
        setup=$HOME/setup_vitis_local.sh
        [[ -r $setup ]] || die "Vitis 2023.2 is not active and $setup is missing"
        printf 'Loading Vitis 2023.2 from %s\n' "$setup"
        set +eu
        # shellcheck source=/dev/null
        source "$setup" 2023.2 >/dev/null
        set -eu
        vpp_version=$(v++ --version 2>&1 || true)
        [[ $vpp_version == *v2023.2* ]] || die "expected Vitis 2023.2, but v++ reports: $vpp_version"
    fi

    course_root=/afs/ece.cmu.edu/class/ece643/software/xilinxVitis
    platform=$course_root/Vitis/2023.2/base_platforms/u96v2_sbc_base_2023_2/export/u96v2_sbc_base_2023_2-vitis-platform/u96v2_sbc_base_2023_2-vitis-platform.xpfm
    platform_sw=$course_root/platforms/2023.2/u96v2_sbc_base_2023_2/sw
    sysroot=$platform_sw/sysroots/cortexa72-cortexa53-xilinx-linux
    cross_cxx=$XILINX_VITIS/gnu/aarch64/lin/aarch64-linux/bin/aarch64-linux-gnu-g++

    for required in "$platform" "$cross_cxx" "$platform_sw/Image" "$platform_sw/rootfs.ext4" \
            "$ws/18643_cnn_src/krnl_cnn.cpp" "$ws/krnl_cnn/hls_config.cfg" \
            "$ws/cnn/hw_link/cnn-link.cfg" "${host_files[@]/#/$ws/cnn_host/}"; do
        [[ -e $required ]] || die "required file is missing: $required"
    done

    # The Vitis IDE compiles 18643_cnn_src/krnl_cnn.cpp in place, so its
    # #include "kernel643.h" finds the copy next to it before cnn_host/'s.
    for h in util643.h kernel643.h instance643.h; do
        if ! cmp -s "$ws/18643_cnn_src/$h" "$ws/cnn_host/$h"; then
            warn "18643_cnn_src/$h differs from cnn_host/$h. This script uses cnn_host/$h;" \
                 "a Vitis IDE build of the kernel probably uses 18643_cnn_src/$h."
        fi
    done

    # --- stage sources --------------------------------------------------------
    if [[ $clean == true ]]; then
        rm -rf -- "$out"
    fi
    src=$out/src
    rm -rf -- "$src"
    mkdir -p "$src"
    cp -- "$ws/18643_cnn_src/krnl_cnn.cpp" "$src/"
    for f in "${host_files[@]}"; do
        cp -- "$ws/cnn_host/$f" "$src/"
    done
    if [[ -n $variant_dir ]]; then
        overridden=0
        for f in krnl_cnn.cpp "${host_files[@]}"; do
            [[ -f $variant_dir/$f ]] || continue
            cp -- "$variant_dir/$f" "$src/"
            overridden=$((overridden + 1))
            case $f in
                krnl_cnn.cpp|kernel643.h) printf '  variant overrides %s\n' "$f" ;;
                *) printf '  variant overrides %s  (not allowed in the final submission)\n' "$f" ;;
            esac
        done
        (( overridden > 0 )) || die "$variant_dir contains none of: krnl_cnn.cpp ${host_files[*]}"
    fi

    build_start=$SECONDS

    if [[ $host_only == false ]]; then
        {
            printf 'name:      %s\n' "$name"
            printf 'variant:   %s\n' "${variant_dir:-(workspace sources)}"
            printf 'built:     %s on %s\n' "$(date '+%F %T')" "$(hostname -s)"
            printf 'git:       %s\n' "$(git -C "$ws" describe --always --dirty 2>/dev/null || echo 'no commits')"
            printf 'hls clock: %s\n' "${hls_clock:+${hls_clock}ns (--hls-clock)}${hls_clock:-from hls_config.cfg}"
            printf 'pl clock:  %s\n' "${pl_clock:+PL$pl_clock (--pl-clock)}${pl_clock:-from cnn-link.cfg}"
            (cd "$src" && md5sum -- *)
        } > "$out/BUILD_INFO"

        # --- 1. HLS -----------------------------------------------------------
        hls_dir=$out/hls
        rm -rf -- "$hls_dir"
        mkdir -p "$hls_dir"
        hls_cfg=$hls_dir/hls_config.cfg
        # Keep every IDE setting (part, clock, uncertainty, top) but point the
        # source list at the staged copies and the .xo at build/NAME/.
        awk -v src="$src" -v clk="$hls_clock" -v xo="$out/krnl_cnn.xo" '
            /^[[:space:]]*(syn\.file|package\.output\.file)[[:space:]]*=/ { next }
            clk != "" && /^[[:space:]]*clock[[:space:]]*=/ { next }
            { print }
            /^[[:space:]]*\[hls\]/ {
                print "syn.file=" src "/krnl_cnn.cpp"
                print "syn.file=" src "/util643.h"
                print "syn.file=" src "/kernel643.h"
                print "syn.file=" src "/instance643.h"
                print "package.output.file=" xo
                if (clk != "") print "clock=" clk "ns"
            }' "$ws/krnl_cnn/hls_config.cfg" > "$hls_cfg"
        grep -q '^syn\.file=' "$hls_cfg" || die "krnl_cnn/hls_config.cfg has no [hls] section"

        rm -f -- "$out/krnl_cnn.xo"
        step 1/4 "HLS: synthesizing krnl_cnn ($(grep -m1 -E '^clock=' "$hls_cfg" || echo 'default clock'), $(grep -m1 -E '^clock_uncertainty=' "$hls_cfg" || echo 'default uncertainty'))"
        # The part comes from hls_config.cfg; v++ rejects --platform alongside it.
        (cd "$hls_dir" && v++ --compile --mode hls \
            --config "$hls_cfg" \
            --work_dir "$hls_dir/work")
        # In 2023.2, v++ --mode hls only synthesizes; exporting the .xo is separate.
        (cd "$hls_dir" && vitis-run --mode hls --package \
            --config "$hls_cfg" \
            --work_dir "$hls_dir/work")
        [[ -f $out/krnl_cnn.xo ]] || die "HLS finished but $out/krnl_cnn.xo is missing"

        # --- 2. link ----------------------------------------------------------
        link_dir=$out/link
        rm -rf -- "$link_dir"
        mkdir -p "$link_dir"
        link_cfg=$link_dir/cnn-link.cfg
        if [[ -n $pl_clock ]]; then
            awk '/^[[:space:]]*\[/ { skip = /^[[:space:]]*\[clock\]/ } !skip' \
                "$ws/cnn/hw_link/cnn-link.cfg" > "$link_cfg"
            printf '\n[clock]\nid=%s:krnl_cnn_1\n' "$pl_clock" >> "$link_cfg"
        else
            cp -- "$ws/cnn/hw_link/cnn-link.cfg" "$link_cfg"
        fi

        rm -f -- "$out/cnn.xclbin"
        step 2/4 "Linking cnn.xclbin (Vivado place-and-route -- the long step)"
        # cnn-link.cfg uses relative temp/report/log dirs, so run from link_dir.
        (cd "$link_dir" && v++ --link --target hw \
            --platform "$platform" \
            --config "$link_cfg" \
            --remote_ip_cache "$out/ipcache" \
            -o "$out/cnn.xclbin" \
            "$out/krnl_cnn.xo")
        [[ -f $out/cnn.xclbin ]] || die "link finished but $out/cnn.xclbin is missing"

        timing_rpt=$(find "$link_dir" -name '*timing_summary*routed*.rpt' -printf '%T@ %p\n' 2>/dev/null |
            sort -n | tail -1 | cut -d' ' -f2-)
        util_rpt=$(find "$link_dir" -name '*full_util_routed.rpt' 2>/dev/null | head -1)
        wns=
        if [[ -n $timing_rpt ]]; then
            wns=$(awk '/WNS\(ns\)/ { getline; getline; print $1; exit }' "$timing_rpt")
        fi
        if [[ $wns =~ ^-?[0-9]+(\.[0-9]+)?$ ]] && awk -v w="$wns" 'BEGIN { exit !(w >= 0) }'; then
            timing_line="timing met (WNS $wns ns)"
        else
            timing_line="TIMING NOT MET (WNS ${wns:-unknown} ns) -- not a valid result at this clock"
        fi
        printf '%s\n' "$timing_line" | tee -a "$out/BUILD_INFO"
    else
        [[ -f $out/cnn.xclbin ]] || die "--host-only needs an earlier full build of '$name' ($out/cnn.xclbin)"
        if [[ -f $out/kernel.md5 ]] && ! (cd "$src" && md5sum --quiet -c "$out/kernel.md5" >/dev/null 2>&1); then
            die "kernel sources changed since build/$name/cnn.xclbin was built; drop --host-only"
        fi
    fi

    # --- 3. host --------------------------------------------------------------
    step "$([[ $host_only == true ]] && echo host-only || echo 3/4)" "Cross-compiling the ARM64 host"
    "$cross_cxx" \
        --sysroot="$sysroot" \
        -mcpu=cortex-a53 \
        -march=armv8-a+crc \
        -std=c++14 \
        -O2 \
        -g \
        -Wno-deprecated-declarations \
        -Wno-ignored-attributes \
        -I"$sysroot/usr/include" \
        -I"$sysroot/usr/include/xrt" \
        "$src/cnn.cpp" \
        -o "$out/cnn_host" \
        -L"$sysroot/usr/lib" \
        -lxilinxopencl \
        -lxrt_coreutil \
        -lpthread \
        -lrt \
        -ldl

    # --- 4. package -----------------------------------------------------------
    if [[ $host_only == false ]]; then
        pkg_dir=$out/package
        rm -rf -- "$pkg_dir"
        mkdir -p "$pkg_dir"
        step 4/4 "Packaging BOOT.BIN and sd_card.img"
        (cd "$pkg_dir" && v++ --package --target hw \
            --platform "$platform" \
            --package.out_dir "$pkg_dir/out" \
            --package.boot_mode sd \
            --package.rootfs "$platform_sw/rootfs.ext4" \
            --package.kernel_image "$platform_sw/Image" \
            --package.sd_file "$out/cnn_host" \
            --temp_dir "$pkg_dir/tmp" \
            -o "$pkg_dir/cnn.xclbin" \
            "$out/cnn.xclbin")
        [[ -f $boot_bin ]] || die "packaging finished but $boot_bin is missing"

        # Lets a later --host-only check it still matches this xclbin.
        (cd "$src" && md5sum -- "${kernel_files[@]}") > "$out/kernel.md5"
    fi

    mkdir -p "$deploy_dir"
    install -m 755 "$out/cnn_host" "$deploy_dir/${name}_host"
    install -m 644 "$out/cnn.xclbin" "$deploy_dir/${name}.xclbin"

    elapsed=$((SECONDS - build_start))
    printf '\n\e[1mBuild of %s complete\e[0m in %dh%02dm\n' "$name" $((elapsed / 3600)) $((elapsed % 3600 / 60))
    if [[ $host_only == false ]]; then
        printf '  %s\n' "$timing_line"
        printf '  timing report:      %s\n' "${timing_rpt:-(not found)}"
        printf '  utilization report: %s\n' "${util_rpt:-(not found)}"
        printf '  SD card image:      %s\n' "$out/package/out/sd_card.img"
    fi
    printf '  deploy pair:        %s\n' "$deploy_dir/${name}_host" "$deploy_dir/${name}.xclbin"
}

if [[ $deploy_only == true ]]; then
    for f in "$deploy_dir/${name}_host" "$deploy_dir/${name}.xclbin"; do
        [[ -f $f ]] || die "nothing to deploy: $f is missing (build '$name' first)"
    done
    # Missing mid-package; without it the board could pair this xclbin with an old bitstream.
    if [[ $copy_boot == true && ! -f $boot_bin ]]; then
        die "no BOOT.BIN at $boot_bin (build unfinished or still running?); add --no-boot to deploy without it"
    fi
    printf 'Deploying the existing build of %s (no rebuild):\n' "$name"
    if [[ -f $out/BUILD_INFO ]]; then
        sed -E '/^[0-9a-f]{32}  /d; s/^/  /' "$out/BUILD_INFO"
    fi
else
    build
fi

[[ $deploy == true ]] || exit 0

# --- deploy -------------------------------------------------------------------
rsh() { ssh -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 -- "$ssh_target" "$@"; }
remote_md5() { rsh "md5sum '$remote_dir/$1' 2>/dev/null" | cut -d' ' -f1 || true; }
local_md5() { md5sum < "$1" | cut -d' ' -f1; }

printf '\n[deploy] checking %s ...\n' "$ssh_target"
rsh true || die "cannot reach $ssh_target over ssh (is the board powered on and on WiFi?)"

files=("$deploy_dir/${name}_host" "$deploy_dir/${name}.xclbin")
need_reboot=false
if [[ $copy_boot == true && ! -f $boot_bin ]]; then
    warn "no BOOT.BIN at $boot_bin; leaving the board's BOOT.BIN alone"
    copy_boot=false
fi
if [[ $copy_boot == true ]]; then
    # BOOT.BIN carries the bitstream and is only read at boot (see lab1).
    boot_md5=$(local_md5 "$boot_bin")
    if [[ $(remote_md5 BOOT.BIN) == "$boot_md5" ]]; then
        printf '[deploy] BOOT.BIN on the board already matches %s\n' "$name"
        if rsh "test \$(stat -c %Y $remote_dir/BOOT.BIN) -gt \$(( \$(date +%s) - \$(cut -d. -f1 /proc/uptime) ))"; then
            printf '[deploy] ...but it was copied after the last boot, so it is not running yet\n'
            need_reboot=true
        fi
    else
        files+=("$boot_bin")
        need_reboot=true
    fi
fi

printf '[deploy] copying to %s:%s\n' "$ssh_target" "$remote_dir"
for f in "${files[@]}"; do
    printf '           %s\n' "$(basename -- "$f")"
done
scp -q -o ConnectTimeout=10 -- "${files[@]}" "$ssh_target:$remote_dir/"
rsh "chmod 755 '$remote_dir/${name}_host' && sync"

if [[ $run == false ]]; then
    if [[ $need_reboot == true ]]; then
        printf '\n*** The new bitstream is in BOOT.BIN, which is only read at boot. ***\n'
        printf '*** Reboot first, or the board runs the PREVIOUS design:         ***\n'
        printf '      ssh %s reboot     # then wait ~90 s\n' "$ssh_target"
    fi
    printf '\nRun with:\n  ssh %s\n  cd %s\n  ./%s_host %s.xclbin\n' \
        "$ssh_target" "$remote_dir" "$name" "$name"
    exit 0
fi

# --- run ----------------------------------------------------------------------
if [[ $need_reboot == true ]]; then
    old_boot=$(rsh cat /proc/sys/kernel/random/boot_id)
    printf '[run] rebooting the board to load the new bitstream ...\n'
    rsh reboot || true
    deadline=$((SECONDS + 300))
    while :; do
        sleep 10
        new_boot=$(ssh -o ConnectTimeout=5 -o BatchMode=yes -- "$ssh_target" \
            cat /proc/sys/kernel/random/boot_id 2>/dev/null || true)
        [[ -n $new_boot && $new_boot != "$old_boot" ]] && break
        (( SECONDS < deadline )) || die "board did not come back within 5 minutes"
    done
    # The SD card's boot partition can mount a few seconds after sshd starts.
    for _ in {1..12}; do
        rsh "test -x '$remote_dir/${name}_host'" && break
        sleep 5
    done
    printf '[run] board is back up\n'
fi

for f in "${name}_host" "${name}.xclbin"; do
    [[ $(remote_md5 "$f") == "$(local_md5 "$deploy_dir/$f")" ]] ||
        die "$f on the board does not match build/deploy/$f"
done
if [[ $copy_boot == true && $(remote_md5 BOOT.BIN) != "$boot_md5" ]]; then
    die "BOOT.BIN on the board does not match build/$name"
fi

log_dir=$out/runs/$(date +%Y%m%d-%H%M%S)
mkdir -p "$log_dir"
cp -- "$out/BUILD_INFO" "$log_dir/" 2>/dev/null || true
for ((i = 1; i <= runs; i++)); do
    printf '\n[run %d/%d]\n' "$i" "$runs"
    rsh "cd '$remote_dir' && ./${name}_host ${name}.xclbin" 2>&1 | tee "$log_dir/run$i.log" ||
        warn "run $i exited with an error"
done

printf '\n\e[1m%s on %s\e[0m\n' "$name" "$ssh_target"
printf '%-5s %16s %14s  %s\n' run 'runtime (us)' 'GigaOP/s' result
for ((i = 1; i <= runs; i++)); do
    f=$log_dir/run$i.log
    rt=$(awk '/runtime =/ { print $3; exit }' "$f")
    tp=$(awk '/throughput:/ { print $2; exit }' "$f")
    res=$(grep -oE 'TEST (PASSED|FAILED)' "$f" | tail -1 || true)
    printf '%-5s %16s %14s  %s\n' "$i" "${rt:--}" "${tp:--}" "${res:-no result}"
done
printf 'logs: %s\n' "$log_dir"
