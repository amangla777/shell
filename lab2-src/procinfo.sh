#!/bin/bash

#Do not insert code here

#DO NOT REMOVE THE FOLLOWING TWO LINES
git add $0 >> .local.git.out
git commit -a -m "Lab 2 commit" >> .local.git.out
git push >> .local.git.out || echo

# procinfo [-t secs] pattern
#
# prints PID, CMD, USER, Memory Usage, CPU time, and number of threads 
# of processes with a command # that matches "pattern"
#
# If the [-t secs] option is passed, then it will loop and print the information
# every "secs" secods.
#
# If no pattern is given, it prints an error that a pattern is missing.
#

# Function to print usage
usage() {
    echo "Usage: $0 [-t secs] pattern"
    exit 1
}

# Checking if arguments are provided
if [ $# -lt 1 ]; then
    usage
fi

# Variables
interval=0
pattern=""
loop=false

# Parsing arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[0-9]+$ ]]; then
                usage
            fi
            interval="$1"
            loop=true
            shift
            ;;
        *)
            pattern="$1"
            shift
            ;;
    esac
done

# If no pattern provided
if [ -z "$pattern" ]; then
    echo "Error: pattern missing"
    exit 1
fi

# Function to print process information
print_process_info() {
    echo "    PID    CMD          USER         MEM          CPU        THREADS"
    ps -eo pid,comm,user,%mem,time,nlwp --sort=-%mem | grep -i "$pattern" | awk '{printf "%8d %-12s %-10s %6s MB  %10s   %2d Thr\n", $1, $2, $3, $4, $5, $6}'
}

# Execute once or in a loop
if [ "$loop" = true ]; then
    while true; do
        clear
        print_process_info
        sleep "$interval"
    done
else
    print_process_info
fi


