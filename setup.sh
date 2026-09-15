#!/bin/bash
#sudo apt-get update
#sudo apt-get install -y xvfb

# Poco is not bundled with ofxPoco on Linux; it links against the system copy.
# Needed on every Linux ARM target, 32- and 64-bit alike. Skipped when already
# present so re-runs don't need sudo or a network.
if ! dpkg -s libpoco-dev >/dev/null 2>&1; then
    sudo apt-get install -y libpoco-dev
fi

DIR=$PWD

cd ../../../addons
#git clone https://github.com/n1ckfg/ofxCv
#git clone https://github.com/n1ckfg/ofxCvPiCam
git clone https://github.com/n1ckfg/ofxNaplps

git clone https://github.com/n1ckfg/ofxMediaType
git clone https://github.com/n1ckfg/ofxNetworkUtils
git clone https://github.com/n1ckfg/ofxSSLManager
git clone https://github.com/n1ckfg/ofxJSON

ARCH=$(uname -m)
echo Architecture is $ARCH.
if [ "$ARCH" = "aarch64" ]; then
	git clone -b of_0.12.1 https://github.com/n1ckfg/ofxHTTP
	git clone -b of_0.12.1 https://github.com/n1ckfg/ofxIO
	git clone -b of_0.12.1 https://github.com/n1ckfg/ofxCrypto
else
        git clone https://github.com/n1ckfg/ofxHTTP
        git clone https://github.com/n1ckfg/ofxIO
        git clone https://github.com/n1ckfg/ofxCrypto
fi

# ofxPoco (bundled with oF 0.12.1) has no linuxaarch64 section in its
# addon_config.mk, so on 64-bit Pi OS no -lPoco* flags are emitted and every
# Poco symbol fails to link. Add the section if it isn't already there.
if ! grep -q '^linuxaarch64:' ofxPoco/addon_config.mk; then
    python3 - <<'PATCH'
path = "ofxPoco/addon_config.mk"
with open(path) as f:
    text = f.read()

section = """linuxaarch64:
\tADDON_LDFLAGS = -lPocoNetSSL
\tADDON_LDFLAGS += -lPocoNet
\tADDON_LDFLAGS += -lPocoCrypto
\tADDON_LDFLAGS += -lPocoUtil
\tADDON_LDFLAGS += -lPocoJSON
\tADDON_LDFLAGS += -lPocoXML
\tADDON_LDFLAGS += -lPocoZip
\tADDON_LDFLAGS += -lPocoFoundation
\tADDON_LDFLAGS += -lcrypto
\tADDON_LDFLAGS += -lssl

msys2:"""

with open(path, "w") as f:
    f.write(text.replace("msys2:", section, 1))
print("patched ofxPoco/addon_config.mk for linuxaarch64")
PATCH
fi

# ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ 
cd $DIR

# Define the path to the header file
FILE="../../../libs/openFrameworks/types/ofTypes.h"

# Ensure the file exists before trying to modify it
if [ ! -f "$FILE" ]; then
    echo "Error: File not found at $FILE"
    exit 1
fi

# Check if the file already contains #include <memory>
if grep -q "#include <memory>" "$FILE"; then
    echo "The file already contains '#include <memory>'. No changes made."
else
    # Check if #include <mutex> exists so we know where to insert
    if grep -q "#include <mutex>" "$FILE"; then
        echo "Adding '#include <memory>' after '#include <mutex>'..."
        
        # Use sed to append the line after the match
        sed -i '/#include <mutex>/a #include <memory>' "$FILE"
        
        echo "Modification complete."
    else
        echo "Error: '#include <mutex>' not found. Could not determine where to insert."
        exit 1
    fi
fi

# ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~

cd $DIR
