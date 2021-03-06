INCLUDE(FindGTK)

FIND_PATH(K3D_TIFF_INCLUDE_DIR tiff.h
	/usr/include
	c:/gtk/include
	${K3D_GTK_DIR}/include
	DOC "Directory where the libtiff header files are located"
	)
MARK_AS_ADVANCED(K3D_TIFF_INCLUDE_DIR)

SET(K3D_TIFF_LIB tiff CACHE STRING "")
MARK_AS_ADVANCED(K3D_TIFF_LIB)

SET(K3D_TIFF_INCLUDE_DIRS
	${K3D_TIFF_INCLUDE_DIR}
	)

SET(K3D_TIFF_LIBS
	${K3D_TIFF_LIB}
	)

SET(K3D_TIFF_FOUND 1)

