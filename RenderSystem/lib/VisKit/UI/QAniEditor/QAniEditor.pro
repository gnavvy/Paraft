######################################################################
# Automatically generated by qmake (2.01a) ??? ?? 20 15:12:42 2008
######################################################################

TEMPLATE = lib
CONFIG += qt staticlib debug_and_release
DEPENDPATH += .
INCLUDEPATH += . ../../shadermanager ../../camera ../../Render ../QTFEditor
LIBS += ../../shadermanager/libshadermanager.a
QT += opengl

# Input
HEADERS += QAniEditor.h QAniClickable.h QAniKeyframe.h QAniInstance.h QAniGraph.h \
	   ../../Render/QRenderWindow.h \
	   ../../camera/camera.h \
           ../../shadermanager/shader.h \
           ../../shadermanager/shadermanager.h \
           ../../shadermanager/gltexture.h \
	   ../QTFEditor/QTFEditor.h \
	   ../QTFEditor/QTFPanel.h \
	   QAniInterface.h

SOURCES += QAniEditor.cpp QAniClickable.cpp QAniKeyframe.cpp QAniInstance.cpp QAniGraph.cpp
RESOURCES += QAniEditor.qrc