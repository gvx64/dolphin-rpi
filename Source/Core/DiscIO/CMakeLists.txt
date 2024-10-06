set(SRCS
  Blob.cpp
  CISOBlob.cpp
  WbfsBlob.cpp
  CompressedBlob.cpp
  DiscScrubber.cpp
  DriveBlob.cpp
  Enums.cpp
  FileBlob.cpp
  FileSystemGCWii.cpp
  Filesystem.cpp
  LaggedFibonacciGenerator.cpp #gvx64 rollforward to 5.0-12188 - implement .rvz support
  NANDContentLoader.cpp
  NANDImporter.cpp
  TGCBlob.cpp
  Volume.cpp
  VolumeDirectory.cpp
  VolumeGC.cpp
  VolumeWad.cpp
  VolumeWii.cpp
  WIABlob.cpp  #gvx64 rollforward to 5.0-12188 - implement .rvz support
  WIACompression.cpp  #gvx64 rollforward to 5.0-12188 - implement .rvz support
  WIACompression.h  #gvx64 rollforward to 5.0-12188 - implement .rvz support
  WiiWad.cpp
)

add_dolphin_library(discio "${SRCS}" "")

#gvx64 rollforward to 5.0-12188 - implement .rvz support
target_link_libraries(discio
PUBLIC
  BZip2::BZip2
  LibLZMA::LibLZMA
  zstd
PRIVATE
  minizip
  pugixml
  ZLIB::ZLIB
)
