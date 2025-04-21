#!/bin/sh

if [ -z "$1" ]; then 
        exit 1
fi
if [ -z "$2" ]; then 
        exit 1
fi
if [ ! -d "$1" ]; then 
        exit 1
fi

NUM_OF_FILES=$(find "$1" -type f | wc -l)
NUM_OF_MATCHES=$(find "$1" -type f -exec grep "$2" {} \; | wc -l)

echo "The number of files are $NUM_OF_FILES and the number of matching lines are $NUM_OF_MATCHES"
