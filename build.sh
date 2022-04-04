#!/usr/bin/env bash

docker build . -t cross-xpdf
docker run -it -v  $(pwd)/build:/output cross-xpdf /bin/bash -c "rm -rf /output/* && mkdir -p /output/pdftools/ && cp -r /build/pdftools/* /output/pdftools/"
docker build . -f Dockerfile.aarch64 -t cross-xpdf-aarch64
docker run -it -v  $(pwd)/build:/output cross-xpdf-aarch64 /bin/bash -c "mkdir /output/pdftools/ && cp -r /build/pdftools/* /output/pdftools"
cd build/pdftools
tar -cvzf ../pdftools.tar.gz *
