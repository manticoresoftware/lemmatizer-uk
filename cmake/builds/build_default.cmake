# ---------- default ----------
# Above line is mandatory!
# rules to build default zip archive
message ( STATUS "Will create default zip" )

set ( CPACK_GENERATOR "ZIP;TGZ" )

install ( TARGETS lemmatize_uk LIBRARY DESTINATION "lib" ) # adds lib file
