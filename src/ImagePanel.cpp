#include "ImagePanel.h"

#include <wx/dcbuffer.h>

#include <set>

using namespace std;


vector< wxRect > GetCoverage( const wxRect& viewport, const wxRect& canvas, const wxSize& gridSize )
{
    const wxRect clippedViewport( canvas.Intersect( viewport ) );

    vector< wxRect > coverage;
    const int top    = clippedViewport.GetTop()    / gridSize.y;
    const int bottom = clippedViewport.GetBottom() / gridSize.y;
    const int left   = clippedViewport.GetLeft()   / gridSize.x;
    const int right  = clippedViewport.GetRight()  / gridSize.x;
    for( int y = top; y <= bottom; ++y )
    {
        for( int x = left; x <= right; ++x )
        {
            const wxRect candidate( x * gridSize.x, y * gridSize.y, gridSize.x, gridSize.y );
            const wxRect clipped( canvas.Intersect( candidate ) );
            coverage.push_back( clipped );
        }
    }
    return coverage;
}


template< typename T >
T clamp( const T& val, const T& minVal, const T& maxVal )
{
    if( val < minVal )  return minVal;
    if( val > maxVal )  return maxVal;
    return val;
}


wxPoint ClampPosition( const wxRect& viewport, const wxRect& extent )
{
    const wxSize delta( viewport.GetSize() - extent.GetSize() ); 

    wxPoint newTopLeft( viewport.GetPosition() );

    if( delta.x < 0 )
    {
        // viewport smaller than extent
        int minX = extent.GetPosition().x;
        int maxX = ( extent.GetPosition().x + extent.GetSize().x ) - viewport.GetSize().x;
        newTopLeft.x = clamp( newTopLeft.x, minX, maxX );
    }
    else
    {
        // viewport larger than extent
        newTopLeft.x = extent.GetPosition().x - ( delta.x / 2 );
    }

    if( delta.y < 0 )
    {
        // viewport smaller than extent
        int minY = extent.GetPosition().y;
        int maxY = ( extent.GetPosition().y + extent.GetSize().y ) - viewport.GetSize().y;
        newTopLeft.y = clamp( newTopLeft.y, minY, maxY );
    }
    else
    {
        // viewport larger than extent
        newTopLeft.y = extent.GetPosition().y - ( delta.y / 2 );
    }

    return newTopLeft;
}



wxImagePanel::wxImagePanel( wxWindow* parent )
    : wxWindow( parent, wxID_ANY )
    , mBitmapCache( 1024 )   // ~200 MB for 1024 256x256x3 byte tiles
    , mPosition( 0, 0 )
    , mScale( 1.0 )
    , mImageFactory( this )
    , mAnimationTimer( this )
    , mKeyboardTimer( this )
{
    // for wxAutoBufferedPaintDC
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    SetBackgroundColour( *wxBLACK );

    Bind( wxEVT_MOUSEWHEEL  , &wxImagePanel::OnMouseWheel     , this );
    Bind( wxEVT_SIZE        , &wxImagePanel::OnSize           , this );
    Bind( wxEVT_PAINT       , &wxImagePanel::OnPaint          , this );
    Bind( wxEVT_KEY_DOWN    , &wxImagePanel::OnKeyDown        , this );
    Bind( wxEVT_KEY_UP      , &wxImagePanel::OnKeyUp          , this );
    Bind( wxEVT_LEFT_DOWN   , &wxImagePanel::OnButtonDown     , this );
    Bind( wxEVT_RIGHT_DOWN  , &wxImagePanel::OnButtonDown     , this );
    Bind( wxEVT_MIDDLE_DOWN , &wxImagePanel::OnButtonDown     , this );
    Bind( wxEVT_MOTION      , &wxImagePanel::OnMotion         , this );
    Bind( wxEVT_THREAD      , &wxImagePanel::OnThread         , this );
    Bind( wxEVT_TIMER       , &wxImagePanel::OnAnimationTimer , this, mAnimationTimer.GetId() );
    Bind( wxEVT_TIMER       , &wxImagePanel::OnKeyboardTimer  , this, mKeyboardTimer.GetId() );
}


void wxImagePanel::OnSize( wxSizeEvent& event )
{
    mPosition = ClampPosition( mPosition );

    // invalidate entire panel since we need to redraw everything
    Refresh( false );

    // skip the event so sizers can do their thing
    event.Skip();
}

void wxImagePanel::OnMouseWheel( wxMouseEvent& event )
{
    if( event.m_wheelRotation > 0 )
    {
        // zoom in
        SetScale( mScale * 1.1 );
    }
    else if( event.m_wheelRotation < 0 )
    {
        // zoom out
        SetScale( mScale / 1.1 );
    }
    event.Skip();
}


void wxImagePanel::OnButtonDown( wxMouseEvent& event )
{
    if( event.LeftDown() )
    {
        mLeftPositionStart = mPosition;
        mLeftMouseStart = event.GetPosition();
    }
}


void wxImagePanel::OnMotion( wxMouseEvent& event )
{
    if( event.LeftIsDown() && event.Dragging() )
    {
        wxPoint newPos( mLeftPositionStart - ( event.GetPosition() - mLeftMouseStart ) );
        if( newPos != mPosition )
        {
            ScrollToPosition( newPos );
        }
    }
}

void wxImagePanel::OnKeyDown( wxKeyEvent& event )
{
    switch( event.GetKeyCode() )
    {
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN:
            if( !mKeyboardTimer.IsRunning() )
                mKeyboardTimer.Start( 10 );
            break;
        // zoom in
        case '=':
        case WXK_ADD:
        case WXK_NUMPAD_ADD:
            SetScale( mScale * 1.1 );
            break;
        // zoom out
        case '-':
        case WXK_SUBTRACT:
        case WXK_NUMPAD_SUBTRACT:
            SetScale( mScale / 1.1 );
            break;
        case ']':
            IncrementFrame( true );
            break;
        case '[':
            IncrementFrame( false );
            break;
        case 'P':
            Play( true );
            break;
        default:
            break;
    }
    event.Skip();
}


wxPoint wxImagePanel::ClampPosition( const wxPoint& newPos )
{
    if( NULL == mImage )
    {
        return newPos;
    }

    return ::ClampPosition
        (
        wxRect( newPos, GetSize() ),
        wxRect( wxPoint(0,0), mImage->GetSize() * mScale )
        );
}


void wxImagePanel::OnKeyUp( wxKeyEvent& event )
{
    if( NULL == mImage )
    {
        return;
    }

    switch( event.GetKeyCode() )
    {
        // fit image to window
        case 'X':
        case WXK_NUMPAD_MULTIPLY:
            {
                const double scaleWidth = ( GetSize().x / static_cast< double >( mImage->GetWidth() ) );
                const double scaleHeight = ( GetSize().y / static_cast< double >( mImage->GetHeight() ) );
                SetScale( min( scaleWidth, scaleHeight ) );
            }
            break;
        // zoom 1:1
        case 'Z':
        case WXK_NUMPAD_DIVIDE:
            SetScale( 1.0 );
            break;
        default:
            break;
    }
}


void wxImagePanel::ScrollToPosition( const wxPoint& newPos )
{
    const wxPoint clamped = ClampPosition( newPos );
    wxPoint delta( clamped - mPosition );
    ScrollWindow( -delta.x, -delta.y );
    mPosition = clamped;
}


void wxImagePanel::QueueRect( const ExtRect& rect )
{
    // don't queue rects we have cached
    wxBitmapPtr bmpPtr;
    if( mBitmapCache.get( bmpPtr, rect, false ) )
        return;

    // don't queue rects we've already queued
    if( mQueuedRects.end() != mQueuedRects.find( rect ) )
        return;

    mQueuedRects.insert( rect );
    mImageFactory.AddRect( rect );
}


void wxImagePanel::OnPaint( wxPaintEvent& )
{
    wxPaintDC dc(this);
    //wxAutoBufferedPaintDC dc( this );

    if( NULL == mImage )
    {
        dc.Clear();
        return;
    }

    const wxRect viewport( wxRect( mPosition, GetSize() ).Inflate( GetSize() * 0.1 ) );
    mImageFactory.SetVisibleArea( viewport );

    // only clear where we *won't* be drawing image tiles to help prevent flicker
    {
        const wxRect imageRect( -mPosition, mImage->GetSize() * mScale );
        const wxRect viewportRect( wxPoint( 0, 0 ), GetSize() );
        wxRegion region( viewportRect );
        region.Subtract( imageRect );
        dc.SetDeviceClippingRegion( region );
        dc.Clear();
        dc.DestroyClippingRegion();
    }

    dc.SetDeviceOrigin( -mPosition.x, -mPosition.y );

    const wxRect scaledRect( wxPoint( 0, 0 ), mImage->GetSize() * mScale );
    const wxSize gridSize( TILE_SIZE, TILE_SIZE );

    // get the set of tiles we need to draw
    set< wxRect > rectsToDraw;
    for( wxRegionIterator upd( GetUpdateRegion() ); upd.HaveRects(); ++upd )
    {
        wxRect rect( upd.GetRect() );
        rect.SetPosition( rect.GetPosition() + mPosition );

        const vector< wxRect > ret = GetCoverage
            (
            rect,
            scaledRect,
            gridSize
            );
        rectsToDraw.insert( ret.begin(), ret.end() );
    }

    for( const wxRect& srcRect : rectsToDraw )
    {
        ExtRect niceRect( mCurFrame, 0, srcRect );
        wxBitmapPtr niceBmpPtr;
        if( !mAnimationTimer.IsRunning() && !mBitmapCache.get( niceBmpPtr, niceRect ) )
            QueueRect( niceRect );

        ExtRect quickRect( mCurFrame, -1, srcRect );
        wxBitmapPtr quickBmpPtr;
        if( NULL == niceBmpPtr && !mBitmapCache.get( quickBmpPtr, quickRect ) )
            QueueRect( quickRect );

        wxBitmapPtr toRender;
        if( NULL != niceBmpPtr )
            toRender = niceBmpPtr;
        else if( NULL != quickBmpPtr )
            toRender = quickBmpPtr;
        else
            continue;

        dc.DrawBitmap( *toRender, srcRect.GetPosition() );
    }

    mImageFactory.Sort( std::less< ExtRect >() );
}


void wxImagePanel::SetImages( const AnimationFrames& newImages )
{
    if( newImages.empty() )
        return;

    mFrames = newImages;
    mImageFactory.Reset();
    mBitmapCache.clear();

    mCurFrame = 0;
    SetImage( mFrames[ mCurFrame ].mImage );
    SetScale( mScale );

    if( mFrames.size() > 1 )
    {
        Play( false );
    }
}

void wxImagePanel::SetImage( wxSharedPtr< wxImage > newImage )
{
    mImage = newImage;
    mQueuedRects.clear();
    mImageFactory.SetImage( mImage );
    mPosition = ClampPosition( mPosition );
    Refresh( false );
}

void wxImagePanel::SetScale( const double newScale )
{
    mBitmapCache.clear();

    const wxSize curSize( mImage->GetSize() * mScale );
    const wxSize newSize( mImage->GetSize() * newScale );
    const wxSize center( GetSize() * 0.5 );

    // convert current position into image-parametric 
    // (0 to 1) coordinates at the current scale
    wxRealPoint curParametric( mPosition + center );
    curParametric.x /= curSize.x;
    curParametric.y /= curSize.y;

    // use parametric coords with the new scale to figure out where
    // the old coordinates are in the new coordinate system
    const wxRealPoint newCoords
        (
        curParametric.x * newSize.x,
        curParametric.y * newSize.y
        );

    // subtract off the viewport center because mPosition is the
    // location of the top-left corner of the viewport
    const wxRealPoint newPoint = newCoords - center;

    mScale = newScale;
    mPosition = ClampPosition( newPoint );

    // invalidate entire panel since we need to redraw everything
    Refresh( false );

    if( NULL == mImage )
    {
        return;
    }

    mQueuedRects.clear();
    mImageFactory.SetScale( mScale );
}


void wxImagePanel::OnThread( wxThreadEvent& )
{
    wxClientDC dc( this );
    dc.SetDeviceOrigin( -mPosition.x, -mPosition.y );

    ExtRect rect;
    wxSharedPtr< wxImage > image;
    while( mImageFactory.GetImage( rect, image ) )
    {
        mQueuedRects.erase( rect );

        if( NULL == image )
            continue;

        wxBitmapPtr bmp( new wxBitmap( *image ) );
        mBitmapCache.insert( rect, bmp );

        dc.DrawBitmap( *bmp, get<2>( rect ).GetPosition() );
    }
}

void wxImagePanel::Play( bool toggle )
{
    if( mFrames.size() <= 1 )
    {
        return;
    }

    if( toggle && mAnimationTimer.IsRunning() )
    {
        mAnimationTimer.Stop();

        // we're stopping the animation so redraw entire window 
        // to prompt the creation of better-filtered tiles
        Refresh( false );
    }
    else
    {
        if( mFrames[ mCurFrame ].mDelay >= 0 )
        {
            mAnimationTimer.Stop();
            mAnimationTimer.StartOnce( mFrames[ mCurFrame ].mDelay );
        }
    }
}

void wxImagePanel::IncrementFrame( bool forward )
{
    if( mFrames.size() <= 1 )
    {
        return;
    }

    if( forward )
    {
        mCurFrame++;
        if( mCurFrame >= mFrames.size() )
            mCurFrame = 0;
    }
    else
    {
        if( mCurFrame == 0 )
            mCurFrame = mFrames.size() - 1;
        else
            mCurFrame--;
    }

    SetImage( mFrames[ mCurFrame ].mImage );
}

void wxImagePanel::OnAnimationTimer( wxTimerEvent& WXUNUSED( event ) )
{
    IncrementFrame( true );
    Play( false );
}

void wxImagePanel::OnKeyboardTimer( wxTimerEvent& WXUNUSED( event ) )
{
    wxPoint newPos( mPosition );
    const int step = 10 * ( wxGetKeyState( WXK_CONTROL ) ? 10 : 1 );
    bool down = false;
    if( wxGetKeyState( WXK_LEFT  ) )    { newPos += step * wxPoint( -1,  0 ); down = true; }
    if( wxGetKeyState( WXK_RIGHT ) )    { newPos += step * wxPoint(  1,  0 ); down = true; }
    if( wxGetKeyState( WXK_UP    ) )    { newPos += step * wxPoint(  0, -1 ); down = true; }
    if( wxGetKeyState( WXK_DOWN  ) )    { newPos += step * wxPoint(  0,  1 ); down = true; }

    if( !down )
    {
        mKeyboardTimer.Stop();
        return;
    }

    ScrollToPosition( newPos );
}
