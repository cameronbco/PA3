proxy="127.0.0.1:5555";
url="http://www.testingmcafeesites.com/";            
dir=$(echo "$url" | sed 's/http:\/\///' | sed 's/https:\/\///' | sed 's/\//./g');
mkdir "$dir" 2>/dev/null;
cd "$dir";   
echo "$url" > sources.txt;
echo "Fetching links from $url";
curl -fLs "$url" |
grep -Eo '"(http|https)://[a-zA-Z0-9#~.*,/!?=+&_%:-]*"' |
sed 's/https/http/' |
sed 's/"//g' |
sed "s/'//g" >> "sources.txt";
cat "sources.txt" | sed "s/\/$//" | sort | uniq > "sources.txt";
cat "sources.txt" | xargs -I% zsh -c '
    declare -A extension=(
        [text/html]=.html
        [text/plain]=.txt
        [text/css]=.css
        [text/xml]=.xml
        [image/png]=.png
        [image/jpeg]=.jpeg
        [image/gif]=.gif
        [image/svg]=.svg
        [image/x-icon]=.ico
        [text/javascript]=.js
        [application/javascript]=.js
    );
    mimetype=$(curl -fsLI "%" | grep -i content-type: | grep -soE "[a-z]+/[a-z-]+" | uniq);
    filename="$(echo "%" | sed "s/http:\/\///" | sed "s/https:\/\///" | sed "s/\//./g")"
    if [ ! -z "${extension[$mimetype]}" ]; then
        if [ -z $(echo "$filename" | grep -i ${extension[$mimetype]}$) ]; then
            filename="$filename${extension[$mimetype]}";
        fi;
        echo "Downloading http://$filename";
        curl -fLso "$filename" -x "$proxy" "%";
    else;
        echo "Skipping http://$filename. Unknown or missing Content-Type: \"$mimetype\"";
    fi
';
cd ..;
