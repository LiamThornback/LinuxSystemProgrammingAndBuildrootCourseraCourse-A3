if [ -z "$1" ]; then 
        echo "Error: no file provided."
        exit 1
fi
if [ -z "$2" ]; then 
        echo "Error: no content provided."
        exit 1
fi

DIR_PATH=$(dirname "$1")

mkdir -p "$DIR_PATH" $2>/dev/null

echo "$2" > "$1"
