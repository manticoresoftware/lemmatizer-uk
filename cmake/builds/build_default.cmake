# ---------- default ----------
# Above line is mandatory!
# rules to build default zip archive
message ( STATUS "Will create default ZIP" )

set ( CPACK_GENERATOR "ZIP" )

install ( TARGETS LEMMATIZE_UK LIBRARY DESTINATION "lib" ) # adds lib file
