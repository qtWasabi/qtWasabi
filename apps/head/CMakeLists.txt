# qtwasabi-head — the framework's reference head application (V5f):
# FakeHost by default (the standalone pixel/corpus renderer), a remote
# head over GraphQL with --connect.
qt_add_executable(qtwasabi-head
    main.cpp)
qt_add_resources(qtwasabi-head "headshell"
    PREFIX "/qtwasabi/head"
    FILES HeadShell.qml)
target_link_libraries(qtwasabi-head PRIVATE
    qtwasabi
    qtwasabi_head
    qtwasabi_remote
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml)
