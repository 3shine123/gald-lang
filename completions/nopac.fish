# Print an optspec for argparse to handle cmd's options that are independent of any subcommand.
function __fish_ovicc_global_optspecs
    string join \n rewrite-ovic= v/verbose= version= fovic-arc= fno-ovic-arc= fno-checker= eh= fno-libc= no-comments= trace-refcount= trace-max-iters= trace-no-color= backend= o= I= L= S/asm= arch= gen-completions= Werror emit-bridge-header=
end

function __fish_ovicc_needs_command
    # Figure out if the current invocation already has a command.
    set -l cmd (commandline -opc)
    set -e cmd[1]
    argparse -s (__fish_ovicc_global_optspecs) -- $cmd 2>/dev/null
    or return
    if set -q argv[1]
        # Also print the command, so this can be used to figure out what it is.
        echo $argv[1]
        return 1
    end
    return 0
end

function __fish_ovicc_using_subcommand
    set -l cmd (__fish_ovicc_needs_command)
    test -z "$cmd"
    and return 1
    contains -- $cmd[1] $argv
end

complete -c ovicc -n "__fish_ovicc_needs_command" -l rewrite-ovic -d 'transpile to C only (no link)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -s v -l verbose -d 'show verbose transpilation info' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l version -d 'print version and exit' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l fovic-arc -d 'enable ARC (default)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l fno-ovic-arc -d 'disable ARC (MRC)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l fno-checker -d 'skip type checking' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l eh -d 'exception backend: checked (default) or legacy/sjlj' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l ffreestanding -d 'bare-metal/freestanding output' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l nostdinc -d 'pass -nostdinc to the C compiler (headers come from your -I dirs)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l no-comments -d 'omit readability comments in generated C (default: on)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l trace-refcount -d 'print a static reference-count trace (no codegen)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l trace-max-iters -d 'loop iterations simulated in the refcount trace (default 2)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l trace-no-color -d 'disable colors in the refcount trace' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l backend -d 'C compiler backend (clang is the default)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -s o -d 'output path (binary or .c)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -s I -d 'add include dir' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -s L -d 'add lib dir' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -s S -l asm -d 'link a real assembly file (repeatable)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l arch -d 'target arch (e.g. -arch x86_64)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l gen-completions -d 'generate shell completion script (bash|zsh|fish|powershell|elvish)' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -l Werror -d 'promote warnings to errors'
complete -c ovicc -n "__fish_ovicc_needs_command" -l emit-bridge-header -d 'emit a C bridge header for calling Ovic from C' -r
complete -c ovicc -n "__fish_ovicc_needs_command" -a "run" -d 'compile + run, then delete binary'
