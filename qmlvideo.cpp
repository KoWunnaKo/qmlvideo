#include "qmlvideo.h"
#include <QPainter>
#include <GL/glew.h>
#include <qgl.h>
#include <QDebug>
#include <QTimer>
#include <QMutexLocker>
#include <QImage>
#include <vlc/vlc.h>

QmlVideo::QmlVideo(QDeclarativeItem *parent) :
    QDeclarativeItem(parent),
    m_state(Stopped),
    m_paintMode(PaintModeQPainter),
    m_textureId(0),
    m_pbo1(0),
    m_pbo2(0)

{
    //Set up item options
    setFlag(QGraphicsItem::ItemHasNoContents, false);
    setSmooth(true);

    qRegisterMetaType<const libvlc_event_t *>("const libvlc_event_t *");


    //Initialize the VLC library;
    const char *argv[] =
    {
        "--no-xlib", /* tell VLC to not use Xlib */
        "--network-caching=500",
        "--no-video-title-show",
        "--disable-screensaver",
    };
    int argc = sizeof(argv) / sizeof(*argv);
    m_libVlc = libvlc_new(argc,argv);
}

QmlVideo::~QmlVideo(){
    clearUp();
    libvlc_release(m_libVlc);

}

void QmlVideo::clearUp(){
    if(m_mediaPlayer != NULL)libvlc_media_player_stop(m_mediaPlayer);
    if(m_mediaPlayer != NULL)libvlc_media_player_release(m_mediaPlayer);
    switch(m_paintMode)
    {
    case PaintModeQPainter:
        if(m_pixelBuff != NULL)free(m_pixelBuff);
        break;
    case PaintModeTexture:
        if(m_pixelBuff != NULL)free(m_pixelBuff);
        if(m_textureId != 0){glDeleteTextures(1,&m_textureId);}
        break;
    case PaintModePBO:
        if(m_textureId != 0){glDeleteTextures(1,&m_textureId);}
        if(m_pbo1 != 0){glDeleteBuffers(1, &m_pbo1);}
        if(m_pbo2 != 0){glDeleteBuffers(1, &m_pbo2);}
        break;
    }
}

QmlVideo::State QmlVideo::state()
{
    return(m_state);
}

void QmlVideo::play(const QString &fileName)
{
    if(!fileName.isNull())
        setFileName(fileName);

    setState(Playing);
}

void QmlVideo::pause()
{
    setState(Paused);
}

void QmlVideo::stop()
{
    setState(Stopped);
}

void QmlVideo::setState(State state)
{
    switch(state)
    {
    case Stopped:
        if(m_state == Stopped)
            return;
        libvlc_media_player_pause(m_mediaPlayer);
        libvlc_media_player_set_time(m_mediaPlayer, 0);
        break;
    case Playing:
        libvlc_media_player_play(m_mediaPlayer);
        break;
    case Paused:
        if(m_state != Playing)
            play();
        else
            libvlc_media_player_pause(m_mediaPlayer);
        break;
    }
}

QString QmlVideo::fileName()
{
    return(m_fileName);
}

void QmlVideo::setFileName(const QString &fileName)
{
    clearUp();
    if(m_state != Stopped)
        setState(Stopped);
    m_fileName = fileName;

    libvlc_media_t *m;
    m = libvlc_media_new_path(m_libVlc, qPrintable(fileName));
    m_mediaPlayer = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    libvlc_video_set_format_callbacks(m_mediaPlayer, vlcVideoFormatCallback, NULL);
    libvlc_video_set_callbacks(m_mediaPlayer, vlcVideoLockCallBack, vlcVideoUnlockCallback, vlcVideoDisplayCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerOpening, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerBuffering, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerPlaying, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerPaused, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerStopped, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerEndReached, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerEncounteredError, vlcVideoEventCallback, this);
    //libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerTimeChanged, vlcVideoEventCallback, this);
    //libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerPositionChanged, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerSeekableChanged, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerPausableChanged, vlcVideoEventCallback, this);
    libvlc_event_attach(libvlc_media_player_event_manager(m_mediaPlayer), libvlc_MediaPlayerLengthChanged, vlcVideoEventCallback, this);
}

void QmlVideo::paintFrame()
{
    //Just signal that we need to repaint the item.
    update();
}

void QmlVideo::paint(QPainter *p, const QStyleOptionGraphicsItem *style, QWidget *widget)
{
    switch(m_paintMode)
    {
    case PaintModeQPainter:
    {
        QImage img((uchar *)m_pixelBuff, m_width, m_height, QImage::Format_RGB888);
        p->drawImage(boundingRect(), img.rgbSwapped(), QRect(0,0,m_width, m_height));
    }
        break;
    case PaintModePBO:
    case PaintModeTexture:
    {
        p->beginNativePainting();

        //qDebug() << "Paint...";

        QRectF rect = boundingRect();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_textureId);

        glBegin(GL_QUADS);
            glTexCoord2d(0.0,0.0);
            glColor3i(0,0,0);
            glVertex2d(rect.x(), rect.y());
            glTexCoord2d(0.0,1.0);
            glVertex2d(rect.x(), rect.y() + rect.height());
            glTexCoord2d(1.0,1.0);
            glVertex2d(rect.x() + rect.width(), rect.y() + rect.height());
            glTexCoord2d(1.0,0.0);
            glVertex2d(rect.x() + rect.width(), rect.y());
        glEnd();

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        p->endNativePainting();
    }
        break;
    }
}

unsigned int QmlVideo::vlcVideoFormatCallback(void **object, char *chroma, unsigned int *width, unsigned int *height,
                           unsigned int *pitches, unsigned int *lines)
{
    unsigned int retval = 0;
    QmlVideo *instance = (QmlVideo *)*object;
    QMetaObject::invokeMethod(instance, "setupFormat", Qt::BlockingQueuedConnection, Q_RETURN_ARG(quint32, retval),
                              Q_ARG(char *, chroma), Q_ARG(unsigned int *, width), Q_ARG(unsigned int *, height),
                              Q_ARG(unsigned int *, pitches), Q_ARG(unsigned int *, lines));
    return(retval);
}

void *QmlVideo::vlcVideoLockCallBack(void *object, void **planes)
{
    //Lock the pixel mutex, and hand the pixel buffer to VLC
    QmlVideo *instance = (QmlVideo *)object;
    QMutexLocker((instance->m_pixelMutex));
    *planes = (void *)instance->m_pixelBuff;
    return NULL;
}

void QmlVideo::vlcVideoUnlockCallback(void *object, void *picture, void * const *planes)
{
    QmlVideo *instance = (QmlVideo *)object;
    QMetaObject::invokeMethod(instance, "updateTexture", Qt::BlockingQueuedConnection,
                              Q_ARG(void *, picture), Q_ARG(void * const *, planes));
}

void QmlVideo::vlcVideoDisplayCallback(void *object, void *picture)
{
    //Call the paintFrame function in the main thread.
    QmlVideo *instance = (QmlVideo *)object;
    QMetaObject::invokeMethod(instance, "paintFrame", Qt::BlockingQueuedConnection);
}

quint32 QmlVideo::setupFormat(char *chroma, unsigned int *width, unsigned int *height, unsigned int *pitches, unsigned int *lines)
{
    qDebug() << "Got format request:" << chroma << *width << *height;

    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        m_paintMode = PaintModeQPainter;
    }
    else
    {
        if(GLEW_EXT_pixel_buffer_object)
        {
            m_paintMode = PaintModePBO;
        }
        else
        {
            m_paintMode = PaintModeTexture;
        }
    }

    qDebug() << "Paint Mode:" << m_paintMode;

    strcpy(chroma, "RV24");
    pitches[0] = *width * 3;
    lines[0] = *height * 3;
    m_width = *width;
    m_height = *height;

    switch(m_paintMode)
    {
    case PaintModeQPainter:
        m_pixelBuff = (char *)malloc((*width)*(*height)*3);
        break;
    case PaintModeTexture:
        m_pixelBuff = (char *)malloc((*width)*(*height)*3);
        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
        glBindTexture(GL_TEXTURE_2D, 0);
        break;
    case PaintModePBO:
        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenBuffers(1, &m_pbo1);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo1);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, m_width * m_height * 3, 0, GL_STREAM_DRAW);
        m_pixelBuff = (char *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        glGenBuffers(1, &m_pbo2);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo2);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, m_width * m_height * 3, 0, GL_STREAM_DRAW);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        break;
    }

    return(1);
}

void QmlVideo::updateTexture(void *picture, void * const *planes)
{
    switch(m_paintMode)
    {
    case PaintModeQPainter:
        break;
    case PaintModeTexture:
        //qDebug() << "Decode";
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_BGR_EXT, GL_UNSIGNED_BYTE, m_pixelBuff);
        glBindTexture(GL_TEXTURE_2D, 0);
        break;
    case PaintModePBO:
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo1);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo2);
        //Reset the buffer data to make sure we don't block when the buffer is mapped
        glBufferData(GL_PIXEL_UNPACK_BUFFER, m_width * m_height * 3, 0, GL_STREAM_DRAW);
        m_pixelBuff = (char *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        quint32 tmp = m_pbo1;
        m_pbo1 = m_pbo2;
        m_pbo2 = tmp;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo2);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_BGR_EXT, GL_UNSIGNED_BYTE, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        break;
    }
}

void QmlVideo::vlcVideoEventCallback(const libvlc_event_t *event, void *object)
{
    libvlc_event_t *tmp = new libvlc_event_t;
    memcpy(tmp,event,sizeof(libvlc_event_t));

    QmlVideo *instance = (QmlVideo *)object;
    QMetaObject::invokeMethod(instance, "playerEvent", Qt::QueuedConnection,
                              Q_ARG(const libvlc_event_t *, tmp));
}

void QmlVideo::playerEvent(const libvlc_event_t *event)
{
    switch(event->type)
    {
    case libvlc_MediaPlayerEndReached:
        setFileName(fileName());
    case libvlc_MediaPlayerStopped:
        qDebug() << "Stopped";
        m_state=Stopped;
        emit(stateChanged(m_state));
        emit(stopped());
        break;
    case libvlc_MediaPlayerOpening:
        qDebug() << "Opening";
        m_state=Opening;
        emit(stateChanged(m_state));
        break;
    case libvlc_MediaPlayerBuffering:
        qDebug() << "Buffering";
        m_state=Buffering;
        emit(stateChanged(m_state));
        break;
    case libvlc_MediaPlayerPlaying:
        qDebug() << "Playing";
        m_state=Playing;
        emit(stateChanged(m_state));
        emit(playing());
        break;
    case libvlc_MediaPlayerPaused:
        qDebug() << "Paused";
        m_state=Paused;
        emit(stateChanged(m_state));
        emit(paused());
        break;
    default:
        qDebug() << "Event: " << libvlc_event_type_name(event->type);
        break;
    }

    delete event;
}
