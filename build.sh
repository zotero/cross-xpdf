#!/usr/bin/env bash

docker build . -t cross-xpdf
docker run -it -v  $(pwd)/build:/output cross-xpdf /bin/bash -c "rm -rf /output/* && cp -r /build/pdftools.tar.gz /output/"
