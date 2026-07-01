# Backend libraries build with no GUI dependency (core testable without a
# window). GUI layers (ui/app) are added only when XMCAM_BUILD_GUI is ON.
add_subdirectory(core)
# Added in later phases:
#   add_subdirectory(pipeline)   # VideoSource, V4l2Source, GstSource, uploader
#   add_subdirectory(control)    # ControlIntrospector, ControlSet
#   add_subdirectory(export)     # ConfigWriter/Reader, FrameSink
# if(XMCAM_BUILD_GUI)
#   add_subdirectory(ui)
#   add_subdirectory(app)
# endif()
