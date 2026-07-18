foreach(package_file IN LISTS CPACK_PACKAGE_FILES)
  file(
    CHMOD "${package_file}"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  )
endforeach()
