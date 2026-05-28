#!/bin/bash

# Test file for keyword-pair highlighting in NSBEdit

greet() {
    local name="$1"
    if [ -z "$name" ]; then
        echo "No name given"
    elif [ "$name" = "root" ]; then
        echo "Hello, root!"
    else
        echo "Hello, $name"
    fi
}

check_os() {
    case "$OSTYPE" in
        linux*)
            echo "Linux"
            ;;
        darwin*)
            echo "macOS"
            ;;
        msys* | cygwin*)
            echo "Windows"
            ;;
        *)
            echo "Unknown: $OSTYPE"
            ;;
    esac
}

count_down() {
    local n=$1
    while [ $n -gt 0 ]; do
        echo "  while: $n"
        n=$((n - 1))
    done
}

process_items() {
    for item in alpha beta gamma; do
        if [ "$item" = "beta" ]; then
            echo "  skipping beta"
            continue
        fi
        echo "  item: $item"
    done
}

nested_loops() {
    for i in 1 2 3; do
        for j in a b; do
            if [ "$j" = "b" ]; then
                echo "  i=$i j=$j"
            fi
        done
    done
}

run_until() {
    local x=0
    until [ $x -ge 3 ]; do
        echo "  until: $x"
        x=$((x + 1))
    done
}

main() {
    greet "Alice"
    greet ""
    check_os

    echo "Countdown:"
    count_down 3

    echo "Items:"
    process_items

    echo "Nested loops:"
    nested_loops

    echo "Until loop:"
    run_until
}

main
