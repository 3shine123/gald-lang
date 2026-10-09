_ovicc() {
    local i cur prev opts cmd
    COMPREPLY=()
    if [[ "${BASH_VERSINFO[0]}" -ge 4 ]]; then
        cur="$2"
    else
        cur="${COMP_WORDS[COMP_CWORD]}"
    fi
    prev="$3"
    cmd=""
    opts=""

    # Include the current word so `ovicc run<Tab>` (no trailing space) is
    # detected as the `run` subcommand rather than re-offering "run".
    for i in "${COMP_WORDS[@]:0:COMP_CWORD+1}"
    do
        case "${cmd},${i}" in
            ",$1")
                cmd="ovicc"
                ;;
            ovicc,run)
                cmd="ovicc__subcmd__run"
                ;;
            *)
                ;;
        esac
    done

    case "${cmd}" in
        ovicc)
            opts="-v -o -I -L -l -S -Werror -eh -emit-bridge-header -rewrite-ovic --verbose --version -fovic-arc -fno-ovic-arc -fno-checker -ffreestanding -nostdinc -no-comments -trace-refcount -trace-max-iters -trace-no-color -backend -asm -arch --gen-completions run"
            if [[ ${cur} == -* || ${COMP_CWORD} -eq 1 ]] ; then
                COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
                return 0
            fi
            case "${prev}" in
                -rewrite-ovic)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                --verbose)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -v)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                --version)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -fovic-arc)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -fno-ovic-arc)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -fno-checker)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -no-comments)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -trace-refcount)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -trace-max-iters)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -trace-no-color)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -backend)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -o)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -I)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -L)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -l)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -asm)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -S)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                -arch)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                --gen-completions)
                    COMPREPLY=($(compgen -f "${cur}"))
                    return 0
                    ;;
                *)
                    COMPREPLY=()
                    ;;
            esac
            COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
            return 0
            ;;
        ovicc__subcmd__run)
            opts=""
            if [[ ${cur} == -* ]] ; then
                COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
                return 0
            fi
            # If the cursor is still on the word "run" (no trailing space),
            # treat it as already consumed and show all files.
            local file_cur="${cur}"
            if [[ "${file_cur}" == "run" ]]; then file_cur=""; fi
            case "${prev}" in
                *)
                    COMPREPLY=($(compgen -f "${file_cur}"))
                    return 0
                    ;;
            esac
            COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
            return 0
            ;;
    esac
}

if [[ "${BASH_VERSINFO[0]}" -eq 4 && "${BASH_VERSINFO[1]}" -ge 4 || "${BASH_VERSINFO[0]}" -gt 4 ]]; then
    complete -F _ovicc -o nosort -o bashdefault -o default ovicc
else
    complete -F _ovicc -o bashdefault -o default ovicc
fi
