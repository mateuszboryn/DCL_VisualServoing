# # Get list of header files
FILE(GLOB headers *.hpp)
#
# # Install them to include subdirectory
install(
    FILES ${headers}
    DESTINATION include/Types/Mrrocpp_Proxy
    COMPONENT sdk
)
