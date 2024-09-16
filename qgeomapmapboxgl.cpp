/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Copyright (C) 2017 Mapbox, Inc.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtLocation module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qgeomapmapboxgl.h"
#include "qgeomapmapboxgl_p.h"
#include "qsgmapboxglnode.h"
#include "qmapboxglstylechange_p.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFramebufferObject>
#include <QtLocation/private/qdeclarativecirclemapitem_p.h>
#include <QtLocation/private/qdeclarativegeomapitembase_p.h>
#include <QtLocation/private/qdeclarativepolygonmapitem_p.h>
#include <QtLocation/private/qdeclarativepolylinemapitem_p.h>
#include <QtLocation/private/qdeclarativerectanglemapitem_p.h>
#include <QtLocation/private/qgeomapparameter_p.h>
#include <QtLocation/private/qgeoprojection_p.h>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGImageNode>
#include <QtQuick/private/qsgtexture_p.h>
#include <QtQuick/private/qsgcontext_p.h> // for debugging the context name

#include <QMapboxGL>

#include <cmath>

// FIXME: Expose from Mapbox GL constants
#define MBGL_TILE_SIZE 512.0

namespace {

// WARNING! The development token is subject to Mapbox Terms of Services
// and must not be used in production.
static char developmentToken[] =
    "pk.eyJ1IjoicXRzZGsiLCJhIjoiY2l5azV5MHh5MDAwdTMybzBybjUzZnhxYSJ9.9rfbeqPjX2BusLRDXHCOBA";

/**
 * @brief 以e为低的log（）函数
 * 
 */
static const double invLog2 = 1.0 / std::log(2.0);



/**
 * @brief 256的缩放级别
 * 
 * @param zoomLevelFor256 
 * @param tileSize 
 * @return double 
 */
static double zoomLevelFrom256(double zoomLevelFor256, double tileSize)
{
    return std::log(std::pow(2.0, zoomLevelFor256) * 256.0 / tileSize) * invLog2;
}

} // namespace

/**
 * @brief Construct a new QGeoMapMapboxGLPrivate::QGeoMapMapboxGLPrivate object
 * 
 * @param engine 
 */
QGeoMapMapboxGLPrivate::QGeoMapMapboxGLPrivate(QGeoMappingManagerEngineMapboxGL *engine)
    : QGeoMapPrivate(engine, new QGeoProjectionWebMercator)
{
}

/**
 * @brief Destroy the QGeoMapMapboxGLPrivate::QGeoMapMapboxGLPrivate object
 * 
 */
QGeoMapMapboxGLPrivate::~QGeoMapMapboxGLPrivate()
{
}



/**
 * @brief 更新画面
 * 
 * @param node 
 * @param window 
 * @return QSGNode* 
 */
QSGNode *QGeoMapMapboxGLPrivate::updateSceneGraph(QSGNode *node, QQuickWindow *window)
{
    Q_Q(QGeoMapMapboxGL);

    if (m_viewportSize.isEmpty()) {     // 为空的话退出
        delete node;
        return 0;
    }

    QMapboxGL *map = 0;                                                     // 定义了一个QMapboxGL对象
    if (!node) {
        QOpenGLContext *currentCtx = QOpenGLContext::currentContext();      //获取上下文
        if (!currentCtx) {                                                  // 获取上下文失败
            qWarning("QOpenGLContext is NULL!");
            qWarning() << "You are running on QSG backend " << QSGContext::backend();
            qWarning("The MapboxGL plugin works with both Desktop and ES 2.0+ OpenGL versions.");
            qWarning("Verify that your Qt is built with OpenGL, and what kind of OpenGL.");
            qWarning("To force using a specific OpenGL version, check QSurfaceFormat::setRenderableType and QSurfaceFormat::setDefaultFormat");

            return node;
        }
        if (m_useFBO) { // 使用帧缓存对象，OpenGL帧缓存对象(FBO：Frame Buffer Object)
            QSGMapboxGLTextureNode *mbglNode = new QSGMapboxGLTextureNode(m_settings, m_viewportSize, window->devicePixelRatio(), q);
            QObject::connect(mbglNode->map(), &QMapboxGL::mapChanged, q, &QGeoMapMapboxGL::onMapChanged);  // 当地图发生变化时调用地图变化的槽函数
            m_syncState = MapTypeSync | CameraDataSync | ViewportSync | VisibleAreaSync;    // 同步设置
            node = mbglNode;
        } else { // 不使用帧缓存对象，调用QSGMapboxGLRenderNode
            QSGMapboxGLRenderNode *mbglNode = new QSGMapboxGLRenderNode(m_settings, m_viewportSize, window->devicePixelRatio(), q);
            QObject::connect(mbglNode->map(), &QMapboxGL::mapChanged, q, &QGeoMapMapboxGL::onMapChanged);
            m_syncState = MapTypeSync | CameraDataSync | ViewportSync | VisibleAreaSync;
            node = mbglNode;
        }
    }
    map = (m_useFBO) ? static_cast<QSGMapboxGLTextureNode *>(node)->map()
                     : static_cast<QSGMapboxGLRenderNode *>(node)->map();

    if (m_syncState & MapTypeSync) {
        m_developmentMode = m_activeMapType.name().startsWith("mapbox://")
            && m_settings.accessToken() == developmentToken;

        map->setStyleUrl(m_activeMapType.name());  // 设置风格样式
    }

    if (m_syncState & VisibleAreaSync) {
        if (m_visibleArea.isEmpty()) {
            map->setMargins(QMargins());
        } else {
            // QMargins定义了矩形的四个外边距量，left,top,right和bottom，描述围绕矩形的边框宽度。
            QMargins margins(m_visibleArea.x(),                                                     // left
                             m_visibleArea.y(),                                                     // top
                             m_viewportSize.width() - m_visibleArea.width() - m_visibleArea.x(),    // right
                             m_viewportSize.height() - m_visibleArea.height() - m_visibleArea.y()); // bottom
            map->setMargins(margins);
        }
    }

    if (m_syncState & CameraDataSync || m_syncState & VisibleAreaSync) {
        map->setZoom(zoomLevelFrom256(m_cameraData.zoomLevel() , MBGL_TILE_SIZE));      // 设置缩放
        map->setBearing(m_cameraData.bearing());                                        // 设置角度
        map->setPitch(m_cameraData.tilt());                                             // 设置地图的俯仰（倾斜）

        QGeoCoordinate coordinate = m_cameraData.center();                              // QGeoCoordinate 由纬度、经度和可选的海拔高度定义。这里应该是中心的地理位置
        map->setCoordinate(QMapbox::Coordinate(coordinate.latitude(), coordinate.longitude()));     // 设置地理位置
    }

    if (m_syncState & ViewportSync) {
        if (m_useFBO) {
            static_cast<QSGMapboxGLTextureNode *>(node)->resize(m_viewportSize, window->devicePixelRatio());
        } else {
            map->resize(m_viewportSize);
        }
    }

    if (m_styleLoaded) {
        syncStyleChanges(map);
    }

    if (m_useFBO) {
        static_cast<QSGMapboxGLTextureNode *>(node)->render(window);
    }

    threadedRenderingHack(window, map);

    m_syncState = NoSync;

    return node;
}


/**
 * @brief 添加参数
 * 
 * @param param 
 */
void QGeoMapMapboxGLPrivate::addParameter(QGeoMapParameter *param)
{
    Q_Q(QGeoMapMapboxGL);

    QObject::connect(param, &QGeoMapParameter::propertyUpdated, q,
        &QGeoMapMapboxGL::onParameterPropertyUpdated);

    if (m_styleLoaded) {
        m_styleChanges << QMapboxGLStyleChange::addMapParameter(param);
        emit q->sgNodeChanged();
    }
}


/**
 * @brief 删除参数
 * 
 * @param param 
 */
void QGeoMapMapboxGLPrivate::removeParameter(QGeoMapParameter *param)
{
    Q_Q(QGeoMapMapboxGL);

    q->disconnect(param);

    if (m_styleLoaded) {
        m_styleChanges << QMapboxGLStyleChange::removeMapParameter(param);
        emit q->sgNodeChanged();
    }
}

/**
 * @brief 支持的地图图元类型类型
 * 
 * @return QGeoMap::ItemTypes 
 */
QGeoMap::ItemTypes QGeoMapMapboxGLPrivate::supportedMapItemTypes() const
{
    return QGeoMap::MapRectangle | QGeoMap::MapCircle | QGeoMap::MapPolygon | QGeoMap::MapPolyline;
}


/**
 * @brief 添加图元
 * 
 * @param item 
 */
void QGeoMapMapboxGLPrivate::addMapItem(QDeclarativeGeoMapItemBase *item)
{
    Q_Q(QGeoMapMapboxGL);

    switch (item->itemType()) {
    case QGeoMap::NoItem:
    case QGeoMap::MapQuickItem:
    case QGeoMap::CustomMapItem:
        return;
    case QGeoMap::MapRectangle: {
        QDeclarativeRectangleMapItem *mapItem = static_cast<QDeclarativeRectangleMapItem *>(item);
        QObject::connect(mapItem, &QQuickItem::visibleChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeGeoMapItemBase::mapItemOpacityChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeRectangleMapItem::bottomRightChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem, &QDeclarativeRectangleMapItem::topLeftChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem, &QDeclarativeRectangleMapItem::colorChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::colorChanged, q, &QGeoMapMapboxGL::onMapItemSubPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::widthChanged, q, &QGeoMapMapboxGL::onMapItemUnsupportedPropertyChanged);
    } break;
    case QGeoMap::MapCircle: {
        QDeclarativeCircleMapItem *mapItem = static_cast<QDeclarativeCircleMapItem *>(item);
        QObject::connect(mapItem, &QQuickItem::visibleChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeGeoMapItemBase::mapItemOpacityChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeCircleMapItem::centerChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem, &QDeclarativeCircleMapItem::radiusChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem, &QDeclarativeCircleMapItem::colorChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::colorChanged, q, &QGeoMapMapboxGL::onMapItemSubPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::widthChanged, q, &QGeoMapMapboxGL::onMapItemUnsupportedPropertyChanged);
    } break;
    case QGeoMap::MapPolygon: {
        QDeclarativePolygonMapItem *mapItem = static_cast<QDeclarativePolygonMapItem *>(item);
        QObject::connect(mapItem, &QQuickItem::visibleChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeGeoMapItemBase::mapItemOpacityChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativePolygonMapItem::pathChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem, &QDeclarativePolygonMapItem::colorChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::colorChanged, q, &QGeoMapMapboxGL::onMapItemSubPropertyChanged);
        QObject::connect(mapItem->border(), &QDeclarativeMapLineProperties::widthChanged, q, &QGeoMapMapboxGL::onMapItemUnsupportedPropertyChanged);
    } break;
    case QGeoMap::MapPolyline: {
        QDeclarativePolylineMapItem *mapItem = static_cast<QDeclarativePolylineMapItem *>(item);
        QObject::connect(mapItem, &QQuickItem::visibleChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativeGeoMapItemBase::mapItemOpacityChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);
        QObject::connect(mapItem, &QDeclarativePolylineMapItem::pathChanged, q, &QGeoMapMapboxGL::onMapItemGeometryChanged);
        QObject::connect(mapItem->line(), &QDeclarativeMapLineProperties::colorChanged, q, &QGeoMapMapboxGL::onMapItemSubPropertyChanged);
        QObject::connect(mapItem->line(), &QDeclarativeMapLineProperties::widthChanged, q, &QGeoMapMapboxGL::onMapItemSubPropertyChanged);
    } break;
    }

    QObject::connect(item, &QDeclarativeGeoMapItemBase::mapItemOpacityChanged, q, &QGeoMapMapboxGL::onMapItemPropertyChanged);

    m_styleChanges << QMapboxGLStyleChange::addMapItem(item, m_mapItemsBefore);

    emit q->sgNodeChanged();
}

/**
 * @brief 移除图元
 * 
 * @param item 
 */
void QGeoMapMapboxGLPrivate::removeMapItem(QDeclarativeGeoMapItemBase *item)
{
    Q_Q(QGeoMapMapboxGL);

    switch (item->itemType()) {
    case QGeoMap::NoItem:
    case QGeoMap::MapQuickItem:
    case QGeoMap::CustomMapItem:
        return;
    case QGeoMap::MapRectangle:
        q->disconnect(static_cast<QDeclarativeRectangleMapItem *>(item)->border());
        break;
    case QGeoMap::MapCircle:
        q->disconnect(static_cast<QDeclarativeCircleMapItem *>(item)->border());
        break;
    case QGeoMap::MapPolygon:
        q->disconnect(static_cast<QDeclarativePolygonMapItem *>(item)->border());
        break;
    case QGeoMap::MapPolyline:
        q->disconnect(static_cast<QDeclarativePolylineMapItem *>(item)->line());
        break;
    }

    q->disconnect(item);

    m_styleChanges << QMapboxGLStyleChange::removeMapItem(item);

    emit q->sgNodeChanged();
}


/**
 * @brief 修改窗口尺寸
 * 
 */
void QGeoMapMapboxGLPrivate::changeViewportSize(const QSize &)
{
    Q_Q(QGeoMapMapboxGL);

    m_syncState = m_syncState | ViewportSync;
    emit q->sgNodeChanged();
}



void QGeoMapMapboxGLPrivate::changeCameraData(const QGeoCameraData &)
{
    Q_Q(QGeoMapMapboxGL);

    m_syncState = m_syncState | CameraDataSync;
    emit q->sgNodeChanged();
}

void QGeoMapMapboxGLPrivate::changeActiveMapType(const QGeoMapType)
{
    Q_Q(QGeoMapMapboxGL);

    m_syncState = m_syncState | MapTypeSync;
    emit q->sgNodeChanged();
}

/**
 * @brief 设置可见区域
 * 
 * @param visibleArea 
 */
void QGeoMapMapboxGLPrivate::setVisibleArea(const QRectF &visibleArea)
{
    Q_Q(QGeoMapMapboxGL);
    const QRectF va = clampVisibleArea(visibleArea);
    if (va == m_visibleArea)
        return;

    m_visibleArea = va;
    m_geoProjection->setVisibleArea(va);

    m_syncState = m_syncState | VisibleAreaSync;
    emit q->sgNodeChanged();
}


/**
 * @brief 获取可见区域
 * 
 * @return QRectF 
 */
QRectF QGeoMapMapboxGLPrivate::visibleArea() const
{
    return m_visibleArea;
}


/**
 * @brief 同步风格变化
 * 
 * @param map 
 */
void QGeoMapMapboxGLPrivate::syncStyleChanges(QMapboxGL *map)
{
    for (const auto& change : m_styleChanges) {
        change->apply(map);
    }

    m_styleChanges.clear();
}


/**
 * @brief 渲染
 * 
 * @param window 
 * @param map 
 */
void QGeoMapMapboxGLPrivate::threadedRenderingHack(QQuickWindow *window, QMapboxGL *map)
{
    // FIXME: Optimal support for threaded rendering needs core changes
    // in Mapbox GL Native. Meanwhile we need to set a timer to update
    // the map until all the resources are loaded, which is not exactly
    // battery friendly, because might trigger more paints than we need.
    if (!m_warned) {
        m_threadedRendering = window->openglContext()->thread() != QCoreApplication::instance()->thread();

        if (m_threadedRendering) {
            qWarning() << "Threaded rendering is not optimal in the Mapbox GL plugin.";
        }

        m_warned = true;
    }

    if (m_threadedRendering) {
        if (!map->isFullyLoaded()) {
            QMetaObject::invokeMethod(&m_refresh, "start", Qt::QueuedConnection);       // invokeMethod()方法用来调用一个对象的信号、槽、可调用的方法，这是一个静态方法
                                                                                        // 第一个参数是被调用对象的指针；
                                                                                        // 第二个参数是方法的名字；
                                                                                        // 第三个参数是连接类型。可以指定连接类型，来决定是同步还是异步调用。
                                                                                        // 如果type是Qt :: QueuedConnection，则会发送一个QEvent，并在应用程序进入主事件循环后立即调用该成员。
                                                                                        // m_refresh为一个定时器，这里是启动或者停止定时器
                                                                                        // 不是完全加载的时候才会启动定时器
        } else {
            QMetaObject::invokeMethod(&m_refresh, "stop", Qt::QueuedConnection);
        }
    }
}

/*
 * QGeoMapMapboxGL implementation
 */

QGeoMapMapboxGL::QGeoMapMapboxGL(QGeoMappingManagerEngineMapboxGL *engine, QObject *parent)
    :   QGeoMap(*new QGeoMapMapboxGLPrivate(engine), parent), m_engine(engine)
{
    Q_D(QGeoMapMapboxGL);

    connect(&d->m_refresh, &QTimer::timeout, this, &QGeoMap::sgNodeChanged);
    d->m_refresh.setInterval(250);
}

QGeoMapMapboxGL::~QGeoMapMapboxGL()
{
}


/**
 * @brief 样式表
 * 
 * @return QString 
 */
QString QGeoMapMapboxGL::copyrightsStyleSheet() const
{
    return QStringLiteral("* { vertical-align: middle; font-weight: normal }");
}


/**
 * @brief 设置mapboxgl设置
 * 
 * @param settings 
 * @param useChinaEndpoint 
 */
void QGeoMapMapboxGL::setMapboxGLSettings(const QMapboxGLSettings& settings, bool useChinaEndpoint)
{
    Q_D(QGeoMapMapboxGL);

    d->m_settings = settings;

    // If the access token is not set, use the development access token. 使用开发者token
    // This will only affect mapbox:// styles.
    // Mapbox China requires a China-specific access token. // 中国需要特殊的token
    if (d->m_settings.accessToken().isEmpty()) {
        if (useChinaEndpoint) {         
            qWarning("Mapbox China requires an access token: https://www.mapbox.com/contact/sales");
        } else {
            d->m_settings.setAccessToken(developmentToken);
        }
    }
}

void QGeoMapMapboxGL::setUseFBO(bool useFBO)
{
    Q_D(QGeoMapMapboxGL);
    d->m_useFBO = useFBO;
}

void QGeoMapMapboxGL::setMapItemsBefore(const QString &before)
{
    Q_D(QGeoMapMapboxGL);
    d->m_mapItemsBefore = before;
}

QGeoMap::Capabilities QGeoMapMapboxGL::capabilities() const
{
    return Capabilities(SupportsVisibleRegion
                        | SupportsSetBearing
                        | SupportsAnchoringCoordinate
                        | SupportsVisibleArea );
}

QSGNode *QGeoMapMapboxGL::updateSceneGraph(QSGNode *oldNode, QQuickWindow *window)
{
    Q_D(QGeoMapMapboxGL);
    return d->updateSceneGraph(oldNode, window);
}


/**
 * @brief 当地图发生变化时执行的槽函数
 * 
 * @param change 
 */
void QGeoMapMapboxGL::onMapChanged(QMapboxGL::MapChange change)
{
    Q_D(QGeoMapMapboxGL);

    if (change == QMapboxGL::MapChangeDidFinishLoadingStyle || change == QMapboxGL::MapChangeDidFailLoadingMap) {
        d->m_styleLoaded = true;
    } else if (change == QMapboxGL::MapChangeWillStartLoadingMap) {
        d->m_styleLoaded = false;
        d->m_styleChanges.clear();

        for (QDeclarativeGeoMapItemBase *item : d->m_mapItems)
            d->m_styleChanges << QMapboxGLStyleChange::addMapItem(item, d->m_mapItemsBefore);

        for (QGeoMapParameter *param : d->m_mapParameters)
            d->m_styleChanges << QMapboxGLStyleChange::addMapParameter(param);
    }
}

void QGeoMapMapboxGL::onMapItemPropertyChanged()
{
    Q_D(QGeoMapMapboxGL);

    QDeclarativeGeoMapItemBase *item = static_cast<QDeclarativeGeoMapItemBase *>(sender());
    d->m_styleChanges << QMapboxGLStyleSetPaintProperty::fromMapItem(item);
    d->m_styleChanges << QMapboxGLStyleSetLayoutProperty::fromMapItem(item);

    emit sgNodeChanged();
}

void QGeoMapMapboxGL::onMapItemSubPropertyChanged()
{
    Q_D(QGeoMapMapboxGL);

    QDeclarativeGeoMapItemBase *item = static_cast<QDeclarativeGeoMapItemBase *>(sender()->parent());
    d->m_styleChanges << QMapboxGLStyleSetPaintProperty::fromMapItem(item);

    emit sgNodeChanged();
}

void QGeoMapMapboxGL::onMapItemUnsupportedPropertyChanged()
{
    // TODO https://bugreports.qt.io/browse/QTBUG-58872
    qWarning() << "Unsupported property for managed Map item";
}

void QGeoMapMapboxGL::onMapItemGeometryChanged()
{
    Q_D(QGeoMapMapboxGL);

    QDeclarativeGeoMapItemBase *item = static_cast<QDeclarativeGeoMapItemBase *>(sender());
    d->m_styleChanges << QMapboxGLStyleAddSource::fromMapItem(item);

    emit sgNodeChanged();
}

void QGeoMapMapboxGL::onParameterPropertyUpdated(QGeoMapParameter *param, const char *)
{
    Q_D(QGeoMapMapboxGL);

    d->m_styleChanges.append(QMapboxGLStyleChange::addMapParameter(param));

    emit sgNodeChanged();
}

void QGeoMapMapboxGL::copyrightsChanged(const QString &copyrightsHtml)
{
    Q_D(QGeoMapMapboxGL);

    QString copyrightsHtmlFinal = copyrightsHtml;

    if (d->m_developmentMode) {
        copyrightsHtmlFinal.prepend("<a href='https://www.mapbox.com/pricing'>"
            + tr("Development access token, do not use in production.") + "</a> - ");
    }

    if (d->m_activeMapType.name().startsWith("mapbox://")) {
        copyrightsHtmlFinal = "<table><tr><th><img src='qrc:/mapboxgl/logo.png'/></th><th>"
            + copyrightsHtmlFinal + "</th></tr></table>";
    }

    QGeoMap::copyrightsChanged(copyrightsHtmlFinal);
}
