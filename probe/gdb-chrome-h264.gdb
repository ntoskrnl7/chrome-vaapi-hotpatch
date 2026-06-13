set pagination off
set confirm off
set print thread-events off
set follow-fork-mode child
set detach-on-fork off
handle SIGTRAP stop print nopass
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
run --user-data-dir=profile-gdb-h264 --no-first-run --disable-sync --no-sandbox --autoplay-policy=no-user-gesture-required --enable-logging=stderr --vmodule=*vaapi*=4,*video*=3,*media*=3,*webcodecs*=3 --ignore-gpu-blocklist --enable-features=AcceleratedVideoEncoder,VaapiVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxZeroCopyGL,VaapiIgnoreDriverChecks --enable-blink-features=WebCodecs test-media/h264-encode-force-hw.html
printf "\n=== stopped ===\n"
info inferiors
info threads
thread apply all bt 8
printf "\n=== registers ===\n"
info registers rip rsp rbp rax rbx rcx rdx rsi rdi
printf "\n=== pc ===\n"
x/16i $pc-24
printf "\n=== mappings chrome ===\n"
info proc mappings
quit
