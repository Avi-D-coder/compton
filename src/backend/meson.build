# enable xrender
srcs += [ files('xrender.c', 'backend.c', 'backend_common.c') ]

# enable opengl
if get_option('opengl')
  srcs += [ files('gl/gl_common.c') ]
  deps += [ dependency('gl', required: true) ]
  cflags += [ '-DGL_GLEXT_LEGACY', '-DGL_GLEXT_PROTOTYPES' ]
endif
