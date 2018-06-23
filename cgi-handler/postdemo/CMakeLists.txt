aux_source_directory(./ DIR_LIB_SRCS)
add_library (postdemo SHARED ${DIR_LIB_SRCS})

include_directories(
           ../xtra_rhel6.x/include )
