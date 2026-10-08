# Print an optspec for argparse to handle cmd's options that are independent of any subcommand.
function __fish_nepac_global_optspecs
    string join \n rewrite-nepa= v/verbose= version= fnepa-arc= fno-nepa-arc= fno-checker= eh= fno-libc= no-comments= trace-refcount= trace-max-iters= trace-no-color= backend= o= I= L= S/asm= arch= gen-completions= Werror emit-bridge-header=
end

function __fish_nepac_needs_command
    # Figure out if the current invocation already has a command.
    set -l cmd (commandline -opc)
    set -e cmd[1]
    argparse -s (__fish_nepac_global_optspecs) -- $cmd 2>/dev/null
    or return
    if set -q argv[1]
        # Also print the command, so this can be used to figure out what it is.
        echo $argv[1]
        return 1
    end
    return 0
end

function __fish_nepac_using_subcommand
    set -l cmd (__fish_nepac_needs_command)
    test -z "$cmd"
    and return 1
    contains -- $cmd[1] $argv
end

complete -c nepac -n "__fish_nepac_needs_command" -l rewrite-nepa -d 'transpile to C only (no link)' -r
complete -c nepac -n "__fish_nepac_needs_command" -s v -l verbose -d 'show verbose transpilation info' -r
complete -c nepac -n "__fish_nepac_needs_command" -l version -d 'print version and exit' -r
complete -c nepac -n "__fish_nepac_needs_command" -l fnepa-arc -d 'enable ARC (default)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l fno-nepa-arc -d 'disable ARC (MRC)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l fno-checker -d 'skip type checking' -r
complete -c nepac -n "__fish_nepac_needs_command" -l eh -d 'exception backend: checked (default) or legacy/sjlj' -r
complete -c nepac -n "__fish_nepac_needs_command" -l ffreestanding -d 'bare-metal/freestanding output' -r
complete -c nepac -n "__fish_nepac_needs_command" -l nostdinc -d 'pass -nostdinc to the C compiler (headers come from your -I dirs)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l no-comments -d 'omit readability comments in generated C (default: on)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l trace-refcount -d 'print a static reference-count trace (no codegen)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l trace-max-iters -d 'loop iterations simulated in the refcount trace (default 2)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l trace-no-color -d 'disable colors in the refcount trace' -r
complete -c nepac -n "__fish_nepac_needs_command" -l backend -d 'C compiler backend (clang is the default)' -r
complete -c nepac -n "__fish_nepac_needs_command" -s o -d 'output path (binary or .c)' -r
complete -c nepac -n "__fish_nepac_needs_command" -s I -d 'add include dir' -r
complete -c nepac -n "__fish_nepac_needs_command" -s L -d 'add lib dir' -r
complete -c nepac -n "__fish_nepac_needs_command" -s S -l asm -d 'link a real assembly file (repeatable)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l arch -d 'target arch (e.g. -arch x86_64)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l gen-completions -d 'generate shell completion script (bash|zsh|fish|powershell|elvish)' -r
complete -c nepac -n "__fish_nepac_needs_command" -l Werror -d 'promote warnings to errors'
complete -c nepac -n "__fish_nepac_needs_command" -l emit-bridge-header -d 'emit a C bridge header for calling Nepa from C' -r
complete -c nepac -n "__fish_nepac_needs_command" -a "run" -d 'compile + run, then delete binary'
