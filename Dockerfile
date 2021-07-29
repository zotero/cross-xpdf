FROM debian:jessie

LABEL maintainer="Martynas Bagdonas <git.martynas@gmail.com>"

RUN apt-get update \
	&& apt-get -y install \
		git \
		wget \
		build-essential \
		llvm \
		llvm-dev \
		gcc-multilib \
		g++-multilib \
		mingw-w64 \
		cmake \
		automake \
		autogen \
		pkg-config \
		sed \
		clang \
		libxml2-dev \
		patch

RUN mkdir /build \
	&& mkdir /build/darwin_x64 \
	&& mkdir /build/windows_x86 \
	&& mkdir /build/windows_x64 \
	&& mkdir /build/linux_x86 \
	&& mkdir /build/linux_x64

RUN git clone https://github.com/tpoechtrager/osxcross /build/osxcross

COPY MacOSX10.11.sdk.tar.xz /build/osxcross/tarballs/MacOSX10.11.sdk.tar.xz

RUN cd /build/osxcross \
  && git checkout c47ff0aeed1a7d0e1f884812fc170e415f05be5a \
	&& echo | SDK_VERSION=10.11 OSX_VERSION_MIN=10.4 UNATTENDED=1 ./build.sh \
	&& mv /build/osxcross/target /usr/x86_64-apple-darwin15

COPY darwin_x64.cmake /build/darwin_x64.cmake
COPY windows_x86.cmake /build/windows_x86.cmake
COPY windows_x64.cmake /build/windows_x64.cmake

RUN cd /build/ \
	&& wget -O xpdf.tar.gz https://dl.xpdfreader.com/xpdf-4.03.tar.gz \
	&& mkdir xpdf \
	&& tar -xf xpdf.tar.gz -C xpdf --strip-components=1 \
	&& cd xpdf \
	&& sed -i "/^\s\sfixCommandLine(&argc,/a if(argc!=3 || argv[1][0]=='-' || argv[2][0]=='-') {fprintf(stderr,\"This is a custom xpdf pdfinfo build. Please use the original version!\\\\n%s\\\\n%s\\\\npdfinfo <PDF-file> <output-file>\\\\n\",xpdfVersion,xpdfCopyright); return 1;} else {freopen( argv[argc-1], \"w\", stdout); argc--;}" xpdf/pdfinfo.cc

COPY pdftotext.cc /build/xpdf/xpdf/pdftotext.cc
COPY GlobalParams.h /build/xpdf/xpdf/GlobalParams.h
COPY GlobalParams.cc /build/xpdf/xpdf/GlobalParams.cc
COPY gfile.h /build/xpdf/goo/gfile.h
COPY gfile.cc /build/xpdf/goo/gfile.cc
COPY cmake-config.txt /build/xpdf/cmake-config.txt

# macOS 64-bit
RUN cd /build/darwin_x64 \
	&& cmake /build/xpdf \
			-DCMAKE_CXX_FLAGS="-stdlib=libc++ -Os" \
			-DCMAKE_TOOLCHAIN_FILE=../darwin_x64.cmake \
			${COMMON_OPTIONS} \
	&& make

# Windows 32-bit
RUN cd /build/windows_x86 \
	&& cmake /build/xpdf \
			-DCMAKE_CXX_FLAGS="-Os -mwindows" \
			-DCMAKE_EXE_LINKER_FLAGS="-static" \
			-DCMAKE_TOOLCHAIN_FILE=../windows_x86.cmake \
			${COMMON_OPTIONS} \
	&& make

# Windows 64-bit
#RUN cd /build/windows_x64 \
#	&& cmake /build/xpdf \
#			-DCMAKE_CXX_FLAGS="-std=c++11 -Os -mwindows" \
#			-DCMAKE_EXE_LINKER_FLAGS="-static" \
#			-DCMAKE_TOOLCHAIN_FILE=../windows_x64.cmake \
#			${COMMON_OPTIONS} \
#	&& make

# Linux 32-bit
RUN cd /build/linux_x86 \
	&& cmake /build/xpdf \
			-DCMAKE_CXX_FLAGS="-m32 -Os" \
			-DCMAKE_C_FLAGS="-m32 -Os" \
			-DCMAKE_EXE_LINKER_FLAGS="-static -pthread" \
			${COMMON_OPTIONS} \
	&& make

# Linux 64-bit
RUN cd /build/linux_x64 \
	&& cmake /build/xpdf \
			-DCMAKE_CXX_FLAGS="-Os" \
			-DCMAKE_EXE_LINKER_FLAGS="-static -pthread" \
			${COMMON_OPTIONS} \
	&& make

RUN mkdir /build/pdftools \
	&& cd /build/pdftools \
	&& cp /build/darwin_x64/xpdf/pdfinfo ./pdfinfo-mac \
	&& cp /build/darwin_x64/xpdf/pdftotext ./pdftotext-mac \
	&& cp /build/windows_x86/xpdf/pdfinfo.exe ./pdfinfo-win.exe \
	&& cp /build/windows_x86/xpdf/pdftotext.exe ./pdftotext-win.exe \
#	&& cp /build/windows_x64/xpdf/pdfinfo.exe ./pdfinfo_windows_x64.exe \
#	&& cp /build/windows_x64/xpdf/pdftotext.exe ./pdftotext_windows_x64.exe \
	&& cp /build/linux_x86/xpdf/pdfinfo ./pdfinfo-linux-i686 \
	&& cp /build/linux_x86/xpdf/pdftotext ./pdftotext-linux-i686 \
	&& cp /build/linux_x64/xpdf/pdfinfo ./pdfinfo-linux-x86_64 \
	&& cp /build/linux_x64/xpdf/pdftotext ./pdftotext-linux-x86_64

RUN cd /build/ \
	&& wget -O poppler-data.tar.gz https://poppler.freedesktop.org/poppler-data-0.4.10.tar.gz \
	&& mkdir poppler-data \
	&& tar -xf poppler-data.tar.gz -C poppler-data --strip-components=1 \
	&& cd pdftools \
	&& mkdir -p poppler-data \
	&& cd poppler-data \
	&& cp -r ../../poppler-data/cidToUnicode ./ \
	&& cp -r ../../poppler-data/cMap ./ \
	&& cp -r ../../poppler-data/nameToUnicode ./ \
	&& cp -r ../../poppler-data/unicodeMap ./ \
	&& cp -r ../../poppler-data/COPYING ./ \
	&& cp -r ../../poppler-data/COPYING.adobe ./ \
	&& cp -r ../../poppler-data/COPYING.gpl2 ./ \
	&& cd .. \
	&& tar -cvzf ../pdftools.tar.gz *
