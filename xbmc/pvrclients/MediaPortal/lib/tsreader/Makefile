SRCS=ChannelInfo.cpp \
    DeMultiplexer.cpp \
    DvbUtil.cpp \
    FileReader.cpp \
    MemoryBuffer.cpp \
    MemoryReader.cpp \
    MemorySink.cpp \
    MultiFileReader.cpp \
    PacketSync.cpp \
    PatParser.cpp \
    PidTable.cpp \
    PmtParser.cpp \
    RTSPClient.cpp \
    Section.cpp \
    SectionDecoder.cpp \
    Thread.cpp \
    TSHeader.cpp \
    TSReader.cpp \
    WaitEvent.cpp
LIB=tsreader.a

include ../../../Makefile.include

INCLUDES+=-I. -I../../ -I../../../../filesystem
#-I../live555/liveMedia/include -I../live555/BasicUsageEnvironment/include -I../live555/UsageEnvironment/include -I../live555/groupsock/include
#DEFINES+=-DLIVE555

-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))
