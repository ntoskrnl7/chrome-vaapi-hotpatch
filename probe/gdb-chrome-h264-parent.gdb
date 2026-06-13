set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set follow-fork-mode parent
set detach-on-fork off
handle SIGTRAP stop print nopass
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
run --user-data-dir=profile-gdb-h264-parent --no-first-run --disable-sync --no-sandbox --disable-crash-reporter --autoplay-policy=no-user-gesture-required --enable-logging=stderr --vmodule=*vaapi*=4,*video*=3,*media*=3,*webcodecs*=3 --ignore-gpu-blocklist --enable-features=AcceleratedVideoEncoder,VaapiVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxZeroCopyGL,VaapiIgnoreDriverChecks --enable-blink-features=WebCodecs test-media/h264-encode-force-hw.html
printf "\n=== stopped ===\n"
info inferiors
info threads
thread apply all bt 10
printf "\n=== registers ===\n"
info registers rip rsp rbp rax rbx rcx rdx rsi rdi
printf "\n=== pc ===\n"
x/24i $pc-32
printf "\n=== mappings ===\n"
info proc mappings
quit
