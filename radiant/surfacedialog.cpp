/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

//
// Surface Dialog
//
// Leonardo Zide (leo@lokigames.com)
//

#include "surfacedialog.h"

#include "debugging/debugging.h"
#include "warnings.h"

#include "iscenegraph.h"
#include "itexdef.h"
#include "iundo.h"
#include "iselection.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "signal/isignal.h"
#include "generic/object.h"
#include "math/vector.h"
#include "texturelib.h"
#include "shaderlib.h"
#include "stringio.h"
#include "os/path.h"

#include "gtkutil/idledraw.h"
#include "gtkutil/dialog.h"
#include "gtkutil/entry.h"
#include "gtkutil/nonmodal.h"
#include "gtkutil/pointer.h"
#include "gtkutil/glwidget.h"           //Shamus: For Textool
#include "gtkutil/button.h"
#include "map.h"
#include "select.h"
#include "patchmanip.h"
#include "brushmanip.h"
#include "patchdialog.h"
#include "preferences.h"
#include "brush_primit.h"
#include "xywindow.h"
#include "mainframe.h"
#include "gtkdlgs.h"
#include "dialog.h"
#include "brush.h"              //Shamus: for Textool
#include "patch.h"
#include "commands.h"
#include "stream/stringstream.h"
#include "grid.h"
#include "textureentry.h"

//NOTE: Proper functioning of Textool currently requires that the "#if 1" lines in
//      brush_primit.h be changed to "#if 0". add/removeScale screws this up ATM. :-)
//      Plus, Radiant seems to work just fine without that stuff. ;-)

#define TEXTOOL_ENABLED 0

#if TEXTOOL_ENABLED

namespace TexTool
{

//Shamus: Textool function prototypes
gboolean size_allocate( GtkWidget *, GtkAllocation *, gpointer );
gboolean expose( GtkWidget *, GdkEventExpose *, gpointer );
gboolean button_press( GtkWidget *, GdkEventButton *, gpointer );
gboolean button_release( GtkWidget *, GdkEventButton *, gpointer );
gboolean motion( GtkWidget *, GdkEventMotion *, gpointer );
void flipX( GtkToggleButton *, gpointer );
void flipY( GtkToggleButton *, gpointer );

//End Textool function prototypes

//Shamus: Textool globals
GtkWidget * g_textoolWin;
//End Textool globals

void queueDraw(){
	gtk_widget_queue_draw( g_textoolWin );
}

}

#endif

inline void spin_button_set_step( GtkSpinButton* spin, gfloat step ){
#if 1
	gtk_adjustment_set_step_increment( gtk_spin_button_get_adjustment( spin ), step );
#else
	GValue gvalue = GValue_default();
	g_value_init( &gvalue, G_TYPE_DOUBLE );
	g_value_set_double( &gvalue, step );
	g_object_set( G_OBJECT( gtk_spin_button_get_adjustment( spin ) ), "step-increment", &gvalue, NULL );
#endif
}

class Increment
{
	float& m_f;
public:
	GtkSpinButton* m_spin;
	GtkEntry* m_entry;
	Increment( float& f ) : m_f( f ), m_spin( 0 ), m_entry( 0 ){
	}
	void cancel(){
		entry_set_float( m_entry, m_f );
	}
	typedef MemberCaller<Increment, &Increment::cancel> CancelCaller;
	void apply(){
		m_f = static_cast<float>( entry_get_float( m_entry ) );
		spin_button_set_step( m_spin, m_f );
	}
	typedef MemberCaller<Increment, &Increment::apply> ApplyCaller;
};

void SurfaceInspector_GridChange();

class SurfaceInspector : public Dialog
{
	GtkWindow* BuildDialog();

	NonModalEntry m_textureEntry;
	NonModalSpinner m_hshiftSpinner;
	NonModalEntry m_hshiftEntry;
	NonModalSpinner m_vshiftSpinner;
	NonModalEntry m_vshiftEntry;
	NonModalSpinner m_hscaleSpinner;
	NonModalEntry m_hscaleEntry;
	NonModalSpinner m_vscaleSpinner;
	NonModalEntry m_vscaleEntry;
	NonModalSpinner m_rotateSpinner;
	NonModalEntry m_rotateEntry;

	IdleDraw m_idleDraw;

	GtkCheckButton* m_surfaceFlags[32];
	GtkCheckButton* m_contentFlags[32];

	NonModalEntry m_valueEntry;
	GtkEntry* m_valueEntryWidget;
public:
	WindowPositionTracker m_positionTracker;
	WindowPositionTrackerImportStringCaller m_importPosition;
	WindowPositionTrackerExportStringCaller m_exportPosition;

// Dialog Data
	float m_fitHorizontal;
	float m_fitVertical;

	Increment m_hshiftIncrement;
	Increment m_vshiftIncrement;
	Increment m_hscaleIncrement;
	Increment m_vscaleIncrement;
	Increment m_rotateIncrement;
	GtkEntry* m_texture;

	SurfaceInspector() :
		m_textureEntry( ApplyShaderCaller( *this ), UpdateCaller( *this ) ),
		m_hshiftSpinner( ApplyTexdef_HShiftCaller( *this ), UpdateCaller( *this ) ),
		m_hshiftEntry( Increment::ApplyCaller( m_hshiftIncrement ), Increment::CancelCaller( m_hshiftIncrement ) ),
		m_vshiftSpinner( ApplyTexdef_VShiftCaller( *this ), UpdateCaller( *this ) ),
		m_vshiftEntry( Increment::ApplyCaller( m_vshiftIncrement ), Increment::CancelCaller( m_vshiftIncrement ) ),
		m_hscaleSpinner( ApplyTexdef_HScaleCaller( *this ), UpdateCaller( *this ) ),
		m_hscaleEntry( Increment::ApplyCaller( m_hscaleIncrement ), Increment::CancelCaller( m_hscaleIncrement ) ),
		m_vscaleSpinner( ApplyTexdef_VScaleCaller( *this ), UpdateCaller( *this ) ),
		m_vscaleEntry( Increment::ApplyCaller( m_vscaleIncrement ), Increment::CancelCaller( m_vscaleIncrement ) ),
		m_rotateSpinner( ApplyTexdef_RotationCaller( *this ), UpdateCaller( *this ) ),
		m_rotateEntry( Increment::ApplyCaller( m_rotateIncrement ), Increment::CancelCaller( m_rotateIncrement ) ),
		m_idleDraw( UpdateCaller( *this ) ),
		m_valueEntry( ApplyFlagsCaller( *this ), UpdateCaller( *this ) ),
		m_importPosition( m_positionTracker ),
		m_exportPosition( m_positionTracker ),
		m_hshiftIncrement( g_si_globals.shift[0] ),
		m_vshiftIncrement( g_si_globals.shift[1] ),
		m_hscaleIncrement( g_si_globals.scale[0] ),
		m_vscaleIncrement( g_si_globals.scale[1] ),
		m_rotateIncrement( g_si_globals.rotate ){
		m_fitVertical = 1;
		m_fitHorizontal = 1;
		m_positionTracker.setPosition( WindowPosition( -1, -1, 300, 400 ) );
	}

	void constructWindow( GtkWindow* main_window ){
		m_parent = main_window;
		Create();
		AddGridChangeCallback( FreeCaller<SurfaceInspector_GridChange>() );
	}
	void destroyWindow(){
		Destroy();
	}
	bool visible() const {
		return gtk_widget_get_visible( GTK_WIDGET( GetWidget() ) );
	}
	void queueDraw(){
		if ( visible() ) {
			m_idleDraw.queueDraw();
		}
	}

	void Update();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::Update> UpdateCaller;
	void ApplyShader();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyShader> ApplyShaderCaller;

//void ApplyTexdef();
//typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef> ApplyTexdefCaller;
	void ApplyTexdef_HShift();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef_HShift> ApplyTexdef_HShiftCaller;
	void ApplyTexdef_VShift();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef_VShift> ApplyTexdef_VShiftCaller;
	void ApplyTexdef_HScale();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef_HScale> ApplyTexdef_HScaleCaller;
	void ApplyTexdef_VScale();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef_VScale> ApplyTexdef_VScaleCaller;
	void ApplyTexdef_Rotation();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyTexdef_Rotation> ApplyTexdef_RotationCaller;

	void ApplyFlags();
	typedef MemberCaller<SurfaceInspector, &SurfaceInspector::ApplyFlags> ApplyFlagsCaller;
};

namespace
{
SurfaceInspector* g_SurfaceInspector;

inline SurfaceInspector& getSurfaceInspector(){
	ASSERT_NOTNULL( g_SurfaceInspector );
	return *g_SurfaceInspector;
}
}

void SurfaceInspector_constructWindow( GtkWindow* main_window ){
	getSurfaceInspector().constructWindow( main_window );
}
void SurfaceInspector_destroyWindow(){
	getSurfaceInspector().destroyWindow();
}

void SurfaceInspector_queueDraw(){
	getSurfaceInspector().queueDraw();
}

namespace
{
CopiedString g_selectedShader;
TextureProjection g_selectedTexdef;
ContentsFlagsValue g_selectedFlags;
size_t g_selectedShaderSize[2];
}

void SurfaceInspector_SetSelectedShader( const char* shader ){
	g_selectedShader = shader;
	SurfaceInspector_queueDraw();
}

void SurfaceInspector_SetSelectedTexdef( const TextureProjection& projection ){
	g_selectedTexdef = projection;
	SurfaceInspector_queueDraw();
}

void SurfaceInspector_SetSelectedFlags( const ContentsFlagsValue& flags ){
	g_selectedFlags = flags;
	SurfaceInspector_queueDraw();
}

static bool s_texture_selection_dirty = false;

void SurfaceInspector_updateSelection(){
	s_texture_selection_dirty = true;
	SurfaceInspector_queueDraw();

#if TEXTOOL_ENABLED
	if ( g_bp_globals.m_texdefTypeId == TEXDEFTYPEID_BRUSHPRIMITIVES ) {
		TexTool::queueDraw();
		//globalOutputStream() << "textool texture changed..\n";
	}
#endif
}

void SurfaceInspector_SelectionChanged( const Selectable& selectable ){
	SurfaceInspector_updateSelection();
}

void SurfaceInspector_SetCurrent_FromSelected(){
	if ( s_texture_selection_dirty == true ) {
		s_texture_selection_dirty = false;
		if ( !g_SelectedFaceInstances.empty() ) {
			TextureProjection projection;
//This *may* be the point before it fucks up... Let's see!
//Yep, there was a call to removeScale in there...
			Scene_BrushGetTexdef_Component_Selected( GlobalSceneGraph(), projection );

			SurfaceInspector_SetSelectedTexdef( projection );

			Scene_BrushGetShaderSize_Component_Selected( GlobalSceneGraph(), g_selectedShaderSize[0], g_selectedShaderSize[1] );
			g_selectedTexdef.m_brushprimit_texdef.coords[0][2] = float_mod( g_selectedTexdef.m_brushprimit_texdef.coords[0][2], (float)g_selectedShaderSize[0] );
			g_selectedTexdef.m_brushprimit_texdef.coords[1][2] = float_mod( g_selectedTexdef.m_brushprimit_texdef.coords[1][2], (float)g_selectedShaderSize[1] );

			CopiedString name;
			Scene_BrushGetShader_Component_Selected( GlobalSceneGraph(), name );
			if ( string_not_empty( name.c_str() ) ) {
				SurfaceInspector_SetSelectedShader( name.c_str() );
			}

			ContentsFlagsValue flags;
			Scene_BrushGetFlags_Component_Selected( GlobalSceneGraph(), flags );
			SurfaceInspector_SetSelectedFlags( flags );
		}
		else
		{
			TextureProjection projection;
			Scene_BrushGetTexdef_Selected( GlobalSceneGraph(), projection );
			SurfaceInspector_SetSelectedTexdef( projection );

			CopiedString name;
			Scene_BrushGetShader_Selected( GlobalSceneGraph(), name );
			if ( string_empty( name.c_str() ) ) {
				Scene_PatchGetShader_Selected( GlobalSceneGraph(), name );
			}
			if ( string_not_empty( name.c_str() ) ) {
				SurfaceInspector_SetSelectedShader( name.c_str() );
			}

			ContentsFlagsValue flags( 0, 0, 0, false );
			Scene_BrushGetFlags_Selected( GlobalSceneGraph(), flags );
			SurfaceInspector_SetSelectedFlags( flags );
		}
	}
}

const char* SurfaceInspector_GetSelectedShader(){
	SurfaceInspector_SetCurrent_FromSelected();
	return g_selectedShader.c_str();
}

const TextureProjection& SurfaceInspector_GetSelectedTexdef(){
	SurfaceInspector_SetCurrent_FromSelected();
	return g_selectedTexdef;
}

const ContentsFlagsValue& SurfaceInspector_GetSelectedFlags(){
	SurfaceInspector_SetCurrent_FromSelected();
	return g_selectedFlags;
}



/*
   ===================================================

   SURFACE INSPECTOR

   ===================================================
 */

si_globals_t g_si_globals;


// make the shift increments match the grid settings
// the objective being that the shift+arrows shortcuts move the texture by the corresponding grid size
// this depends on a scale value if you have selected a particular texture on which you want it to work:
// we move the textures in pixels, not world units. (i.e. increment values are in pixel)
// depending on the texture scale it doesn't take the same amount of pixels to move of GetGridSize()
// increment * scale = gridsize
// hscale and vscale are optional parameters, if they are zero they will be set to the default scale
// NOTE: the default scale depends if you are using BP mode or regular.
// For regular it's 0.5f (128 pixels cover 64 world units), for BP it's simply 1.0f
// see fenris #2810
void DoSnapTToGrid( float hscale, float vscale ){
	g_si_globals.shift[0] = static_cast<float>( float_to_integer( static_cast<float>( GetGridSize() ) / hscale ) );
	g_si_globals.shift[1] = static_cast<float>( float_to_integer( static_cast<float>( GetGridSize() ) / vscale ) );
	getSurfaceInspector().queueDraw();
}

void SurfaceInspector_GridChange(){
	if ( g_si_globals.m_bSnapTToGrid ) {
		DoSnapTToGrid( Texdef_getDefaultTextureScale(), Texdef_getDefaultTextureScale() );
	}
}

// make the shift increments match the grid settings
// the objective being that the shift+arrows shortcuts move the texture by the corresponding grid size
// this depends on the current texture scale used?
// we move the textures in pixels, not world units. (i.e. increment values are in pixel)
// depending on the texture scale it doesn't take the same amount of pixels to move of GetGridSize()
// increment * scale = gridsize
static void OnBtnMatchGrid( GtkWidget *widget, gpointer data ){
	float hscale, vscale;
	hscale = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_hscaleIncrement.m_spin ) );
	vscale = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_vscaleIncrement.m_spin ) );

	if ( hscale == 0.0f || vscale == 0.0f ) {
		globalErrorStream() << "ERROR: unexpected scale == 0.0f\n";
		return;
	}

	DoSnapTToGrid( hscale, vscale );
}

// DoSurface will always try to show the surface inspector
// or update it because something new has been selected
// Shamus: It does get called when the SI is hidden, but not when you select something new. ;-)
void DoSurface( void ){
	if ( getSurfaceInspector().GetWidget() == 0 ) {
		getSurfaceInspector().Create();

	}
	getSurfaceInspector().Update();
	//getSurfaceInspector().importData(); //happens in .ShowDlg() anyway
	getSurfaceInspector().ShowDlg();
}

void SurfaceInspector_toggleShown(){
	if ( getSurfaceInspector().visible() ) {
		getSurfaceInspector().HideDlg();
	}
	else
	{
		DoSurface();
	}
}

#include "camwindow.h"

enum EProjectTexture
{
	eProjectAxial = 0,
	eProjectOrtho = 1,
	eProjectCam = 2,
};

void SurfaceInspector_ProjectTexture( GtkWidget* widget, EProjectTexture type ){
	if ( g_bp_globals.m_texdefTypeId == TEXDEFTYPEID_QUAKE )
		globalWarningStream() << "function doesn't work for *brushes*, having Axial Projection type\n"; //works for patches

	texdef_t texdef;
	if( widget ){ //gui buttons
		getSurfaceInspector().exportData();
		texdef.shift[0] = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_hshiftIncrement.m_spin ) );
		texdef.shift[1] = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_vshiftIncrement.m_spin ) );
		texdef.scale[0] = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_hscaleIncrement.m_spin ) );
		texdef.scale[1] = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_vscaleIncrement.m_spin ) );
		texdef.rotate = static_cast<float>( gtk_spin_button_get_value( getSurfaceInspector().m_rotateIncrement.m_spin ) );
	}
	else{ //bind
		texdef.scale[0] = texdef.scale[1] = Texdef_getDefaultTextureScale();
	}

	StringOutputStream str( 32 );
	str << "textureProject" << ( type == eProjectAxial? "Axial" : type == eProjectOrtho? "Ortho" : "Cam" );
	UndoableCommand undo( str.c_str() );

	Vector3 direction;

	switch ( type )
	{
	case eProjectAxial:
		return Select_ProjectTexture( texdef, 0 );
	case eProjectOrtho:
		direction = g_vector3_axes[GlobalXYWnd_getCurrentViewType()];
		break;
	case eProjectCam:
		//direction = -g_pParentWnd->GetCamWnd()->getCamera().vpn ;
		direction = -Camera_getViewVector( *g_pParentWnd->GetCamWnd() );
		break;
	}

	Select_ProjectTexture( texdef, &direction );
}

void SurfaceInspector_ProjectTexture_eProjectAxial(){
	SurfaceInspector_ProjectTexture( 0, eProjectAxial );
}
void SurfaceInspector_ProjectTexture_eProjectOrtho(){
	SurfaceInspector_ProjectTexture( 0, eProjectOrtho );
}
void SurfaceInspector_ProjectTexture_eProjectCam(){
	SurfaceInspector_ProjectTexture( 0, eProjectCam );
}

void SurfaceInspector_ResetTexture(){
	UndoableCommand undo( "textureReset/Cap" );
	TextureProjection projection;
	TexDef_Construct_Default( projection );

#if TEXTOOL_ENABLED

	//Shamus:
	if ( g_bp_globals.m_texdefTypeId == TEXDEFTYPEID_BRUSHPRIMITIVES ) {
		// Scale up texture width/height if in BP mode...
//NOTE: This may not be correct any more! :-P
		if ( !g_SelectedFaceInstances.empty() ) {
			Face & face = g_SelectedFaceInstances.last().getFace();
			float x = face.getShader().m_state->getTexture().width;
			float y = face.getShader().m_state->getTexture().height;
			projection.m_brushprimit_texdef.coords[0][0] /= x;
			projection.m_brushprimit_texdef.coords[0][1] /= y;
			projection.m_brushprimit_texdef.coords[1][0] /= x;
			projection.m_brushprimit_texdef.coords[1][1] /= y;
		}
	}
#endif

	Select_SetTexdef( projection, false, true );
	Scene_PatchCapTexture_Selected( GlobalSceneGraph() );
}

static void OnBtnPatchCap( GtkWidget *widget, gpointer data ){
	Patch_CapTexture();
}

static void OnBtnPatchNatural( GtkWidget *widget, gpointer data ){
	Patch_NaturalTexture();
}

static void OnBtnPatchFit( GtkWidget *widget, gpointer data ){
	Patch_FitTexture();
}

static void OnBtnPatchFit11( GtkWidget *widget, gpointer data ){
	Patch_FitTexture11();
}

static void OnBtnReset( GtkWidget *widget, gpointer data ){
	SurfaceInspector_ResetTexture();
}

static void OnBtnProject( GtkWidget *widget, EProjectTexture type ){
	SurfaceInspector_ProjectTexture( widget, type );
}


void SurfaceInspector_FitTexture(){
	UndoableCommand undo( "textureAutoFit" );
	getSurfaceInspector().exportData();
	Select_FitTexture( getSurfaceInspector().m_fitHorizontal, getSurfaceInspector().m_fitVertical );
}

void SurfaceInspector_FaceFitWidth(){
	UndoableCommand undo( "textureAutoFitWidth" );
	getSurfaceInspector().exportData();
	Select_FitTexture( getSurfaceInspector().m_fitHorizontal, 0 );
}

void SurfaceInspector_FaceFitHeight(){
	UndoableCommand undo( "textureAutoFitHeight" );
	getSurfaceInspector().exportData();
	Select_FitTexture( 0, getSurfaceInspector().m_fitVertical );
}

void SurfaceInspector_FaceFitWidthOnly(){
	UndoableCommand undo( "textureAutoFitWidthOnly" );
	getSurfaceInspector().exportData();
	Select_FitTexture( getSurfaceInspector().m_fitHorizontal, 0, true );
}

void SurfaceInspector_FaceFitHeightOnly(){
	UndoableCommand undo( "textureAutoFitHeightOnly" );
	getSurfaceInspector().exportData();
	Select_FitTexture( 0, getSurfaceInspector().m_fitVertical, true );
}


static void OnBtnFaceFit( GtkWidget *widget, gpointer data ){
	SurfaceInspector_FitTexture();
}

static void OnBtnFaceFitWidth( GtkWidget *widget, gpointer data ){
	SurfaceInspector_FaceFitWidth();
}

static void OnBtnFaceFitHeight( GtkWidget *widget, gpointer data ){
	SurfaceInspector_FaceFitHeight();
}

static gboolean OnBtnFaceFitWidthOnly( GtkWidget *widget, GdkEventButton *event, gpointer data ){
	if ( event->button == 3 && event->type == GDK_BUTTON_PRESS ) {
		SurfaceInspector_FaceFitWidthOnly();
		return TRUE;
	}
	return FALSE;
}

static gboolean OnBtnFaceFitHeightOnly( GtkWidget *widget, GdkEventButton *event, gpointer data ){
	if ( event->button == 3 && event->type == GDK_BUTTON_PRESS ) {
		SurfaceInspector_FaceFitHeightOnly();
		return TRUE;
	}
	return FALSE;
}

static void OnBtnUnsetFlags( GtkWidget *widget, gpointer data ){
	UndoableCommand undo( "flagsUnSetSelected" );
	Select_SetFlags( ContentsFlagsValue( 0, 0, 0, false ) );
}


typedef const char* FlagName;

const FlagName surfaceflagNamesDefault[32] = {
	"surf1",
	"surf2",
	"surf3",
	"surf4",
	"surf5",
	"surf6",
	"surf7",
	"surf8",
	"surf9",
	"surf10",
	"surf11",
	"surf12",
	"surf13",
	"surf14",
	"surf15",
	"surf16",
	"surf17",
	"surf18",
	"surf19",
	"surf20",
	"surf21",
	"surf22",
	"surf23",
	"surf24",
	"surf25",
	"surf26",
	"surf27",
	"surf28",
	"surf29",
	"surf30",
	"surf31",
	"surf32"
};

const FlagName contentflagNamesDefault[32] = {
	"cont1",
	"cont2",
	"cont3",
	"cont4",
	"cont5",
	"cont6",
	"cont7",
	"cont8",
	"cont9",
	"cont10",
	"cont11",
	"cont12",
	"cont13",
	"cont14",
	"cont15",
	"cont16",
	"cont17",
	"cont18",
	"cont19",
	"cont20",
	"cont21",
	"cont22",
	"cont23",
	"cont24",
	"cont25",
	"cont26",
	"cont27",
	"cont28",
	"cont29",
	"cont30",
	"cont31",
	"cont32"
};

const char* getSurfaceFlagName( std::size_t bit ){
	const char* value = g_pGameDescription->getKeyValue( surfaceflagNamesDefault[bit] );
	if ( string_empty( value ) ) {
		return surfaceflagNamesDefault[bit];
	}
	return value;
}

const char* getContentFlagName( std::size_t bit ){
	const char* value = g_pGameDescription->getKeyValue( contentflagNamesDefault[bit] );
	if ( string_empty( value ) ) {
		return contentflagNamesDefault[bit];
	}
	return value;
}


// =============================================================================
// SurfaceInspector class

guint togglebutton_connect_toggled( GtkToggleButton* button, const Callback& callback ){
	return g_signal_connect_swapped( G_OBJECT( button ), "toggled", G_CALLBACK( callback.getThunk() ), callback.getEnvironment() );
}

GtkWindow* SurfaceInspector::BuildDialog(){
	GtkWindow* window = create_floating_window( "Surface Inspector", m_parent );

	m_positionTracker.connect( window );

	global_accel_connect_window( window );

	window_connect_focus_in_clear_focus_widget( window );


	{
		// replaced by only the vbox:
		GtkWidget* vbox = gtk_vbox_new( FALSE, 5 );
		gtk_widget_show( vbox );
		gtk_container_add( GTK_CONTAINER( window ), GTK_WIDGET( vbox ) );
		gtk_container_set_border_width( GTK_CONTAINER( vbox ), 5 );

		{
			GtkWidget* hbox2 = gtk_hbox_new( FALSE, 5 );
			gtk_widget_show( hbox2 );
			gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( hbox2 ), FALSE, FALSE, 0 );

			{
				GtkWidget* label = gtk_label_new( "Texture" );
				gtk_widget_show( label );
				gtk_box_pack_start( GTK_BOX( hbox2 ), label, FALSE, TRUE, 0 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_box_pack_start( GTK_BOX( hbox2 ), GTK_WIDGET( entry ), TRUE, TRUE, 0 );
				m_texture = entry;
				m_textureEntry.connect( entry );
				GlobalTextureEntryCompletion::instance().connect( entry );
			}
		}


		{
			GtkWidget* table = gtk_table_new( 6, 4, FALSE );
			gtk_widget_show( table );
			gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( table ), FALSE, FALSE, 0 );
			gtk_table_set_row_spacings( GTK_TABLE( table ), 5 );
			gtk_table_set_col_spacings( GTK_TABLE( table ), 5 );
			{
				GtkWidget* label = gtk_label_new( "Horizontal shift" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 0, 1,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 0, -8192, 8192, 2, 8, 0 ) ), 0, 2 ) );
				m_hshiftIncrement.m_spin = spin;
				m_hshiftSpinner.connect( spin );
				gtk_widget_show( GTK_WIDGET( spin ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( spin ), 1, 2, 0, 1,
				                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( spin ), 60, -1 );
			}
			{
				GtkWidget* label = gtk_label_new( "Step" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 2, 3, 0, 1,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( entry ), 3, 4, 0, 1,
				                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( entry ), 50, -1 );
				m_hshiftIncrement.m_entry = entry;
				m_hshiftEntry.connect( entry );
			}
			{
				GtkWidget* label = gtk_label_new( "Vertical shift" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 1, 2,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 0, -8192, 8192, 2, 8, 0 ) ), 0, 2 ) );
				m_vshiftIncrement.m_spin = spin;
				m_vshiftSpinner.connect( spin );
				gtk_widget_show( GTK_WIDGET( spin ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( spin ), 1, 2, 1, 2,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( spin ), 60, -1 );
			}
			{
				GtkWidget* label = gtk_label_new( "Step" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 2, 3, 1, 2,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( entry ), 3, 4, 1, 2,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( entry ), 50, -1 );
				m_vshiftIncrement.m_entry = entry;
				m_vshiftEntry.connect( entry );
			}
			{
				GtkWidget* label = gtk_label_new( "Horizontal stretch" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 2, 3,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 0, -8192, 8192, 2, 8, 0 ) ), 0, 5 ) );
				m_hscaleIncrement.m_spin = spin;
				m_hscaleSpinner.connect( spin );
				gtk_widget_show( GTK_WIDGET( spin ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( spin ), 1, 2, 2, 3,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( spin ), 60, -1 );
			}
			{
				GtkWidget* label = gtk_label_new( "Step" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 2, 3, 2, 3,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 2, 3 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( entry ), 3, 4, 2, 3,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 2, 3 );
				gtk_widget_set_size_request( GTK_WIDGET( entry ), 50, -1 );
				m_hscaleIncrement.m_entry = entry;
				m_hscaleEntry.connect( entry );
			}
			{
				GtkWidget* label = gtk_label_new( "Vertical stretch" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 3, 4,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 0, -8192, 8192, 2, 8, 0 ) ), 0, 5 ) );
				m_vscaleIncrement.m_spin = spin;
				m_vscaleSpinner.connect( spin );
				gtk_widget_show( GTK_WIDGET( spin ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( spin ), 1, 2, 3, 4,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( spin ), 60, -1 );
			}
			{
				GtkWidget* label = gtk_label_new( "Step" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 2, 3, 3, 4,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( entry ), 3, 4, 3, 4,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( entry ), 50, -1 );
				m_vscaleIncrement.m_entry = entry;
				m_vscaleEntry.connect( entry );
			}
			{
				GtkWidget* label = gtk_label_new( "Rotate" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 4, 5,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 0, -8192, 8192, 2, 45, 0 ) ), 0, 2 ) );
				m_rotateIncrement.m_spin = spin;
				m_rotateSpinner.connect( spin );
				gtk_widget_show( GTK_WIDGET( spin ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( spin ), 1, 2, 4, 5,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( spin ), 60, -1 );
				gtk_spin_button_set_wrap( spin, TRUE );
			}
			{
				GtkWidget* label = gtk_label_new( "Step" );
				gtk_widget_show( label );
				gtk_misc_set_alignment( GTK_MISC( label ), 0, 0 );
				gtk_table_attach( GTK_TABLE( table ), label, 2, 3, 4, 5,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
			}
			{
				GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
				gtk_widget_show( GTK_WIDGET( entry ) );
				gtk_table_attach( GTK_TABLE( table ), GTK_WIDGET( entry ), 3, 4, 4, 5,
				                  (GtkAttachOptions) ( GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				gtk_widget_set_size_request( GTK_WIDGET( entry ), 50, -1 );
				m_rotateIncrement.m_entry = entry;
				m_rotateEntry.connect( entry );
			}
			{
				// match grid button
				GtkWidget* button = gtk_button_new_with_label( "Match Grid" );
				gtk_widget_show( button );
				gtk_table_attach( GTK_TABLE( table ), button, 2, 4, 5, 6,
				                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
				                  (GtkAttachOptions) ( 0 ), 0, 0 );
				g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( OnBtnMatchGrid ), 0 );
			}
		}

		{
			GtkWidget* frame = gtk_frame_new( "Texturing" );
			gtk_widget_show( frame );
			gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( frame ), FALSE, FALSE, 0 );
			{
				GtkWidget* table = gtk_table_new( 5, 4, FALSE );
				gtk_widget_show( table );
				gtk_container_add( GTK_CONTAINER( frame ), table );
				gtk_table_set_row_spacings( GTK_TABLE( table ), 5 );
				gtk_table_set_col_spacings( GTK_TABLE( table ), 5 );
				gtk_container_set_border_width( GTK_CONTAINER( table ), 5 );
				{
					GtkWidget* label = gtk_label_new( "Brush" );
					gtk_widget_show( label );
					gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 0, 1,
					                  (GtkAttachOptions) ( GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
				}
				{
					GtkWidget* label = gtk_label_new( "Patch" );
					gtk_widget_show( label );
					gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 3, 4,
					                  (GtkAttachOptions) ( GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Width" );
					gtk_widget_set_tooltip_text( button, "Fit texture width, scale height\nRightClick: fit width, keep height" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 2, 3, 0, 1,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnFaceFitWidth ), 0 );
					g_signal_connect( G_OBJECT( button ), "button_press_event", G_CALLBACK( OnBtnFaceFitWidthOnly ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Height" );
					gtk_widget_set_tooltip_text( button, "Fit texture height, scale width\nRightClick: fit height, keep width" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 3, 4, 0, 1,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnFaceFitHeight ), 0 );
					g_signal_connect( G_OBJECT( button ), "button_press_event", G_CALLBACK( OnBtnFaceFitHeightOnly ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Reset" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 0, 1, 1, 2,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnReset ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Fit" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 1, 2, 1, 2,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnFaceFit ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* label = gtk_label_new( "Project:" );
					gtk_widget_show( label );
					gtk_table_attach( GTK_TABLE( table ), label, 0, 1, 2, 3,
					                  (GtkAttachOptions) ( GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Axial" );
					gtk_widget_set_tooltip_text( button, "Axial projection (along nearest axis)" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 1, 2, 2, 3,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ), 0, 5 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnProject ), (gpointer)eProjectAxial );
					GtkRequisition req;
					gtk_widget_size_request( button, &req );
					gtk_widget_set_size_request( button, 60, req.height * 3 / 4 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Ortho" );
					gtk_widget_set_tooltip_text( button, "Project along active ortho view" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 2, 3, 2, 3,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ), 0, 5 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnProject ), (gpointer)eProjectOrtho );
					GtkRequisition req;
					gtk_widget_size_request( button, &req );
					gtk_widget_set_size_request( button, 60, req.height * 3 / 4 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Cam" );
					gtk_widget_set_tooltip_text( button, "Project along camera view direction" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 3, 4, 2, 3,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ), 0, 5 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnProject ), (gpointer)eProjectCam );
					GtkRequisition req;
					gtk_widget_size_request( button, &req );
					gtk_widget_set_size_request( button, 60, req.height * 3 / 4 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "CAP" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 0, 1, 4, 5,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnPatchCap ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Set..." );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 1, 2, 4, 5,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnPatchFit ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Natural" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 2, 3, 4, 5,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnPatchNatural ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* button = gtk_button_new_with_label( "Fit" );
					gtk_widget_show( button );
					gtk_table_attach( GTK_TABLE( table ), button, 3, 4, 4, 5,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					g_signal_connect( G_OBJECT( button ), "clicked",
					                  G_CALLBACK( OnBtnPatchFit11 ), 0 );
					gtk_widget_set_size_request( button, 60, -1 );
				}
				{
					GtkWidget* spin = gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 1, 0, 1 << 16, 1, 10, 0 ) ), 0, 3 );
					gtk_widget_show( spin );
					gtk_table_attach( GTK_TABLE( table ), spin, 2, 3, 1, 2,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_size_request( spin, 60, -1 );
					AddDialogData( *GTK_SPIN_BUTTON( spin ), m_fitHorizontal );
				}
				{
					GtkWidget* spin = gtk_spin_button_new( GTK_ADJUSTMENT( gtk_adjustment_new( 1, 0, 1 << 16, 1, 10, 0 ) ), 0, 3 );
					gtk_widget_show( spin );
					gtk_table_attach( GTK_TABLE( table ), spin, 3, 4, 1, 2,
					                  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
					                  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_size_request( spin, 60, -1 );
					AddDialogData( *GTK_SPIN_BUTTON( spin ), m_fitVertical );
				}
			}
		}
		if ( !string_empty( g_pGameDescription->getKeyValue( "si_flags" ) ) ) {
			{
				GtkFrame* frame = GTK_FRAME( gtk_frame_new( "Surface Flags" ) );
				gtk_widget_show( GTK_WIDGET( frame ) );
				gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( frame ), TRUE, TRUE, 0 );
				{
					GtkVBox* vbox3 = GTK_VBOX( gtk_vbox_new( FALSE, 4 ) );
					//gtk_container_set_border_width(GTK_CONTAINER(vbox3), 4);
					gtk_widget_show( GTK_WIDGET( vbox3 ) );
					gtk_container_add( GTK_CONTAINER( frame ), GTK_WIDGET( vbox3 ) );
					{
						GtkTable* table = GTK_TABLE( gtk_table_new( 8, 4, FALSE ) );
						gtk_widget_show( GTK_WIDGET( table ) );
						gtk_box_pack_start( GTK_BOX( vbox3 ), GTK_WIDGET( table ), TRUE, TRUE, 0 );
						gtk_table_set_row_spacings( table, 0 );
						gtk_table_set_col_spacings( table, 0 );

						GtkCheckButton** p = m_surfaceFlags;

						for ( int c = 0; c != 4; ++c )
						{
							for ( int r = 0; r != 8; ++r )
							{
								GtkCheckButton* check = GTK_CHECK_BUTTON( gtk_check_button_new_with_label( getSurfaceFlagName( c * 8 + r ) ) );
								gtk_widget_show( GTK_WIDGET( check ) );
								gtk_table_attach( table, GTK_WIDGET( check ), c, c + 1, r, r + 1,
								                  (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ),
								                  (GtkAttachOptions)( 0 ), 0, 0 );
								*p++ = check;
								guint handler_id = togglebutton_connect_toggled( GTK_TOGGLE_BUTTON( check ), ApplyFlagsCaller( *this ) );
								g_object_set_data( G_OBJECT( check ), "handler", gint_to_pointer( handler_id ) );
							}
						}
					}
				}
			}
			{
				GtkFrame* frame = GTK_FRAME( gtk_frame_new( "Content Flags" ) );
				gtk_widget_show( GTK_WIDGET( frame ) );
				gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( frame ), TRUE, TRUE, 0 );
				{
					GtkVBox* vbox3 = GTK_VBOX( gtk_vbox_new( FALSE, 4 ) );
					//gtk_container_set_border_width(GTK_CONTAINER(vbox3), 4);
					gtk_widget_show( GTK_WIDGET( vbox3 ) );
					gtk_container_add( GTK_CONTAINER( frame ), GTK_WIDGET( vbox3 ) );
					{

						GtkTable* table = GTK_TABLE( gtk_table_new( 8, 4, FALSE ) );
						gtk_widget_show( GTK_WIDGET( table ) );
						gtk_box_pack_start( GTK_BOX( vbox3 ), GTK_WIDGET( table ), TRUE, TRUE, 0 );
						gtk_table_set_row_spacings( table, 0 );
						gtk_table_set_col_spacings( table, 0 );

						GtkCheckButton** p = m_contentFlags;

						for ( int c = 0; c != 4; ++c )
						{
							for ( int r = 0; r != 8; ++r )
							{
								GtkCheckButton* check = GTK_CHECK_BUTTON( gtk_check_button_new_with_label( getContentFlagName( c * 8 + r ) ) );
								gtk_widget_show( GTK_WIDGET( check ) );
								gtk_table_attach( table, GTK_WIDGET( check ), c, c + 1, r, r + 1,
								                  (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ),
								                  (GtkAttachOptions)( 0 ), 0, 0 );
								*p++ = check;
								guint handler_id = togglebutton_connect_toggled( GTK_TOGGLE_BUTTON( check ), ApplyFlagsCaller( *this ) );
								g_object_set_data( G_OBJECT( check ), "handler", gint_to_pointer( handler_id ) );
							}
						}

						// not allowed to modify detail flag using Surface Inspector
						gtk_widget_set_sensitive( GTK_WIDGET( m_contentFlags[BRUSH_DETAIL_FLAG] ), FALSE );
					}
				}
			}
			{
				GtkFrame* frame = GTK_FRAME( gtk_frame_new( "Value" ) );
				gtk_widget_show( GTK_WIDGET( frame ) );
				gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( frame ), TRUE, TRUE, 0 );
				{
					GtkVBox* hbox3 = GTK_VBOX( gtk_hbox_new( FALSE, 4 ) );
					gtk_container_set_border_width( GTK_CONTAINER( hbox3 ), 4 );
					gtk_widget_show( GTK_WIDGET( hbox3 ) );
					gtk_container_add( GTK_CONTAINER( frame ), GTK_WIDGET( hbox3 ) );

					{
						GtkEntry* entry = GTK_ENTRY( gtk_entry_new() );
						gtk_widget_show( GTK_WIDGET( entry ) );
						gtk_box_pack_start( GTK_BOX( hbox3 ), GTK_WIDGET( entry ), TRUE, TRUE, 0 );
						m_valueEntryWidget = entry;
						m_valueEntry.connect( entry );
					}
					{
						GtkWidget* button = gtk_button_new_with_label( "Unset" );
						gtk_widget_set_tooltip_text( button, "Unset flags" );
						gtk_widget_show( button );
						gtk_box_pack_start( GTK_BOX( hbox3 ), button, TRUE, TRUE, 0 );
						g_signal_connect( G_OBJECT( button ), "clicked",
						                  G_CALLBACK( OnBtnUnsetFlags ), 0 );
						GtkRequisition req;
						gtk_widget_size_request( button, &req );
						gtk_widget_set_size_request( button, 60, req.height * 3 / 4 );
					}
				}
			}
		}

#if TEXTOOL_ENABLED
		if ( g_bp_globals.m_texdefTypeId == TEXDEFTYPEID_BRUSHPRIMITIVES ) {
// Shamus: Textool goodies...
			GtkWidget * frame = gtk_frame_new( "Textool" );
			gtk_widget_show( frame );
			gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( frame ), FALSE, FALSE, 0 );
			{
				//Prolly should make this a member or global var, so the SI can draw on it...
				TexTool::g_textoolWin = glwidget_new( FALSE );
				// --> Dunno, but this stuff may be necessary... (Looks like it!)
				g_object_ref( G_OBJECT( TexTool::g_textoolWin ) );
				gtk_widget_set_events( TexTool::g_textoolWin, GDK_DESTROY | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK );
				gtk_widget_set_can_focus( TexTool::g_textoolWin, TRUE );
				// <-- end stuff...
				gtk_widget_show( TexTool::g_textoolWin );
				gtk_widget_set_size_request( TexTool::g_textoolWin, -1, 240 ); //Yeah!
				gtk_container_add( GTK_CONTAINER( frame ), TexTool::g_textoolWin );

				g_signal_connect( G_OBJECT( TexTool::g_textoolWin ), "size_allocate", G_CALLBACK( TexTool::size_allocate ), NULL );
				g_signal_connect( G_OBJECT( TexTool::g_textoolWin ), "expose_event", G_CALLBACK( TexTool::expose ), NULL );
				g_signal_connect( G_OBJECT( TexTool::g_textoolWin ), "button_press_event", G_CALLBACK( TexTool::button_press ), NULL );
				g_signal_connect( G_OBJECT( TexTool::g_textoolWin ), "button_release_event", G_CALLBACK( TexTool::button_release ), NULL );
				g_signal_connect( G_OBJECT( TexTool::g_textoolWin ), "motion_notify_event", G_CALLBACK( TexTool::motion ), NULL );
			}
			{
				GtkWidget * hbox = gtk_hbox_new( FALSE, 5 );
				gtk_widget_show( hbox );
				gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( hbox ), FALSE, FALSE, 0 );
				// Checkboxes go here... (Flip X/Y)
				GtkWidget * flipX = gtk_check_button_new_with_label( "Flip X axis" );
				GtkWidget * flipY = gtk_check_button_new_with_label( "Flip Y axis" );
				gtk_widget_show( flipX );
				gtk_widget_show( flipY );
				gtk_box_pack_start( GTK_BOX( hbox ), flipX, FALSE, FALSE, 0 );
				gtk_box_pack_start( GTK_BOX( hbox ), flipY, FALSE, FALSE, 0 );

//Instead of this, we probably need to create a vbox to put into the frame, then the
//window, then the hbox. !!! FIX !!!
//        gtk_container_add(GTK_CONTAINER(frame), hbox);

//Hmm. Do we really need g_object_set_data? Mebbe not... And we don't! :-)
//        g_object_set_data(G_OBJECT(flipX), "handler", gint_to_pointer(g_signal_connect(G_OBJECT(flipX), "toggled", G_CALLBACK(TexTool::flipX), 0)));
//        g_object_set_data(G_OBJECT(flipY), "handler", gint_to_pointer(g_signal_connect(G_OBJECT(flipY), "toggled", G_CALLBACK(TexTool::flipY), 0)));
//Instead, just do:
				g_signal_connect( G_OBJECT( flipX ), "toggled", G_CALLBACK( TexTool::flipX ), NULL );
				g_signal_connect( G_OBJECT( flipY ), "toggled", G_CALLBACK( TexTool::flipY ), NULL );
			}
		}
#endif
	}

	return window;
}

/*
   ==============
   Update

   Set the fields to the current texdef (i.e. map/texdef -> dialog widgets)
   if faces selected (instead of brushes) -> will read this face texdef, else current texdef
   if only patches selected, will read the patch texdef
   ===============
 */

void spin_button_set_value_no_signal( GtkSpinButton* spin, gdouble value ){
	guint handler_id = gpointer_to_int( g_object_get_data( G_OBJECT( spin ), "handler" ) );
	g_signal_handler_block( G_OBJECT( gtk_spin_button_get_adjustment( spin ) ), handler_id );
	gtk_spin_button_set_value( spin, value );
	g_signal_handler_unblock( G_OBJECT( gtk_spin_button_get_adjustment( spin ) ), handler_id );
}

void spin_button_set_step_increment( GtkSpinButton* spin, gdouble value ){
	gtk_adjustment_set_step_increment( gtk_spin_button_get_adjustment( spin ), value );
}

void SurfaceInspector::Update(){
	const char * name = SurfaceInspector_GetSelectedShader();

	if ( shader_is_texture( name ) ) {
		gtk_entry_set_text( m_texture, shader_get_textureName( name ) );
	}
	else
	{
		gtk_entry_set_text( m_texture, "" );
	}

	texdef_t shiftScaleRotate;
//Shamus: This is where we get into trouble--the BP code tries to convert to a "faked"
//shift, rotate & scale values from the brush face, which seems to screw up for some reason.
//!!! FIX !!!
/*globalOutputStream() << "--> SI::Update. About to do ShiftScaleRotate_fromFace()...\n";
   SurfaceInspector_GetSelectedBPTexdef();
   globalOutputStream() << "BP: (" << g_selectedBrushPrimitTexdef.coords[0][0] << ", " << g_selectedBrushPrimitTexdef.coords[0][1] << ")("
    << g_selectedBrushPrimitTexdef.coords[1][0] << ", " << g_selectedBrushPrimitTexdef.coords[1][1] << ")("
    << g_selectedBrushPrimitTexdef.coords[0][2] << ", " << g_selectedBrushPrimitTexdef.coords[1][2] << ") SurfaceInspector::Update\n";//*/
//Ok, it's screwed up *before* we get here...
	ShiftScaleRotate_fromFace( shiftScaleRotate, SurfaceInspector_GetSelectedTexdef() );

	// normalize again to hide the ridiculously high scale values that get created when using texlock
	shiftScaleRotate.shift[0] = float_mod( shiftScaleRotate.shift[0], (float)g_selectedShaderSize[0] );
	shiftScaleRotate.shift[1] = float_mod( shiftScaleRotate.shift[1], (float)g_selectedShaderSize[1] );

	{
		spin_button_set_value_no_signal( m_hshiftIncrement.m_spin, shiftScaleRotate.shift[0] );
		spin_button_set_step_increment( m_hshiftIncrement.m_spin, g_si_globals.shift[0] );
		entry_set_float( m_hshiftIncrement.m_entry, g_si_globals.shift[0] );
	}

	{
		spin_button_set_value_no_signal( m_vshiftIncrement.m_spin, shiftScaleRotate.shift[1] );
		spin_button_set_step_increment( m_vshiftIncrement.m_spin, g_si_globals.shift[1] );
		entry_set_float( m_vshiftIncrement.m_entry, g_si_globals.shift[1] );
	}

	{
		spin_button_set_value_no_signal( m_hscaleIncrement.m_spin, shiftScaleRotate.scale[0] );
		spin_button_set_step_increment( m_hscaleIncrement.m_spin, g_si_globals.scale[0] );
		entry_set_float( m_hscaleIncrement.m_entry, g_si_globals.scale[0] );
	}

	{
		spin_button_set_value_no_signal( m_vscaleIncrement.m_spin, shiftScaleRotate.scale[1] );
		spin_button_set_step_increment( m_vscaleIncrement.m_spin, g_si_globals.scale[1] );
		entry_set_float( m_vscaleIncrement.m_entry, g_si_globals.scale[1] );
	}

	{
		spin_button_set_value_no_signal( m_rotateIncrement.m_spin, shiftScaleRotate.rotate );
		spin_button_set_step_increment( m_rotateIncrement.m_spin, g_si_globals.rotate );
		entry_set_float( m_rotateIncrement.m_entry, g_si_globals.rotate );
	}

	if ( !string_empty( g_pGameDescription->getKeyValue( "si_flags" ) ) ) {
		ContentsFlagsValue flags( SurfaceInspector_GetSelectedFlags() );

		entry_set_int( m_valueEntryWidget, flags.m_value );

		for ( GtkCheckButton** p = m_surfaceFlags; p != m_surfaceFlags + 32; ++p )
		{
			toggle_button_set_active_no_signal( GTK_TOGGLE_BUTTON( *p ), flags.m_surfaceFlags & ( 1 << ( p - m_surfaceFlags ) ) );
		}

		for ( GtkCheckButton** p = m_contentFlags; p != m_contentFlags + 32; ++p )
		{
			toggle_button_set_active_no_signal( GTK_TOGGLE_BUTTON( *p ), flags.m_contentFlags & ( 1 << ( p - m_contentFlags ) ) );
		}
	}
}

/*
   ==============
   Apply

   Reads the fields to get the current texdef (i.e. widgets -> MAP)
   in brush primitive mode, grab the fake shift scale rot and compute a new texture matrix
   ===============
 */
void SurfaceInspector::ApplyShader(){
	const auto name = StringOutputStream( 256 )( GlobalTexturePrefix_get(), PathCleaned( gtk_entry_get_text( m_texture ) ) );

	// TTimo: detect and refuse invalid texture names (at least the ones with spaces)
	if ( !texdef_name_valid( name.c_str() ) ) {
		globalErrorStream() << "invalid texture name '" << name.c_str() << "'\n";
		SurfaceInspector_queueDraw();
		return;
	}

	UndoableCommand undo( "textureNameSetSelected" );
	Select_SetShader( name.c_str() );
}
#if 0
void SurfaceInspector::ApplyTexdef(){
	texdef_t shiftScaleRotate;

	shiftScaleRotate.shift[0] = static_cast<float>( gtk_spin_button_get_value( m_hshiftIncrement.m_spin ) );
	shiftScaleRotate.shift[1] = static_cast<float>( gtk_spin_button_get_value( m_vshiftIncrement.m_spin ) );
	shiftScaleRotate.scale[0] = static_cast<float>( gtk_spin_button_get_value( m_hscaleIncrement.m_spin ) );
	shiftScaleRotate.scale[1] = static_cast<float>( gtk_spin_button_get_value( m_vscaleIncrement.m_spin ) );
	shiftScaleRotate.rotate = static_cast<float>( gtk_spin_button_get_value( m_rotateIncrement.m_spin ) );

	TextureProjection projection;
//Shamus: This is the other place that screws up, it seems, since it doesn't seem to do the
//conversion from the face (I think) and so bogus values end up in the thing... !!! FIX !!!
//This is actually OK. :-P
	ShiftScaleRotate_toFace( shiftScaleRotate, projection );

	UndoableCommand undo( "textureProjectionSetSelected" );
	Select_SetTexdef( projection );
}
#endif
void SurfaceInspector::ApplyTexdef_HShift(){
	const float value = static_cast<float>( gtk_spin_button_get_value( m_hshiftIncrement.m_spin ) );
	StringOutputStream command;
	command << "textureProjectionSetSelected -hShift " << value;
	UndoableCommand undo( command.c_str() );
	Select_SetTexdef( &value, 0, 0, 0, 0 );
}

void SurfaceInspector::ApplyTexdef_VShift(){
	const float value = static_cast<float>( gtk_spin_button_get_value( m_vshiftIncrement.m_spin ) );
	StringOutputStream command;
	command << "textureProjectionSetSelected -vShift " << value;
	UndoableCommand undo( command.c_str() );
	Select_SetTexdef( 0, &value, 0, 0, 0 );
}

void SurfaceInspector::ApplyTexdef_HScale(){
	const float value = static_cast<float>( gtk_spin_button_get_value( m_hscaleIncrement.m_spin ) );
	StringOutputStream command;
	command << "textureProjectionSetSelected -hScale " << value;
	UndoableCommand undo( command.c_str() );
	Select_SetTexdef( 0, 0, &value, 0, 0 );
}

void SurfaceInspector::ApplyTexdef_VScale(){
	const float value = static_cast<float>( gtk_spin_button_get_value( m_vscaleIncrement.m_spin ) );
	StringOutputStream command;
	command << "textureProjectionSetSelected -vScale " << value;
	UndoableCommand undo( command.c_str() );
	Select_SetTexdef( 0, 0, 0, &value, 0 );
}

void SurfaceInspector::ApplyTexdef_Rotation(){
	const float value = static_cast<float>( gtk_spin_button_get_value( m_rotateIncrement.m_spin ) );
	StringOutputStream command;
	command << "textureProjectionSetSelected -rotation " << static_cast<float>( float_to_integer( value * 100.f ) ) / 100.f;;
	UndoableCommand undo( command.c_str() );
	Select_SetTexdef( 0, 0, 0, 0, &value );
}

void SurfaceInspector::ApplyFlags(){
	unsigned int surfaceflags = 0;
	for ( GtkCheckButton** p = m_surfaceFlags; p != m_surfaceFlags + 32; ++p )
	{
		if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( *p ) ) ) {
			surfaceflags |= ( 1 << ( p - m_surfaceFlags ) );
		}
	}

	unsigned int contentflags = 0;
	for ( GtkCheckButton** p = m_contentFlags; p != m_contentFlags + 32; ++p )
	{
		if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( *p ) ) ) {
			contentflags |= ( 1 << ( p - m_contentFlags ) );
		}
	}

	int value = entry_get_int( m_valueEntryWidget );

	UndoableCommand undo( "flagsSetSelected" );
	Select_SetFlags( ContentsFlagsValue( surfaceflags, contentflags, value, true ) );
}



enum EPasteMode{
	ePasteNone,
	ePasteValues,
	ePasteSeamless,
	ePasteProject,
};

EPasteMode pastemode_for_modifiers( bool shift, bool ctrl ){
	if( shift )
		return ctrl? ePasteProject : ePasteValues;
	else if( ctrl )
		return ePasteSeamless;
	return ePasteNone;
}
bool pastemode_if_setShader( EPasteMode mode, bool alt ){
	return ( mode == ePasteNone ) || !alt;
}


class PatchData
{
	size_t m_width = 0;
	size_t m_height = 0;
	typedef Array<PatchControl> PatchControlArray;
	PatchControlArray m_ctrl;
public:
	void copy( const Patch& patch ){
		m_width = patch.getWidth();
		m_height = patch.getHeight();
		m_ctrl = patch.getControlPoints();
	}
	size_t getWidth() const {
		return m_width;
	}
	size_t getHeight() const {
		return m_height;
	}
	const PatchControl& ctrlAt( size_t row, size_t col ) const {
		return m_ctrl[row * m_width + col];
	}
	const PatchControl *data() const {
		return m_ctrl.data();
	}
};

class FaceTexture
{
public:
	TextureProjection m_projection; //BP part is removeScale()'d
	ContentsFlagsValue m_flags;

	Plane3 m_plane;
	Winding m_winding;
	std::size_t m_width;
	std::size_t m_height;

	PatchData m_patch;

	float m_light;
	Vector3 m_colour;

	enum ePasteSource{
		eBrush,
		ePatch
	} m_pasteSource;

	FaceTexture() : m_plane( 0, 0, 1, 0 ), m_width( 64 ), m_height( 64 ), m_light( 300 ), m_colour( 1, 1, 1 ), m_pasteSource( eBrush ) {
		m_projection.m_basis_s = Vector3( 0.7071067811865, 0.7071067811865, 0 );
		m_projection.m_basis_t = Vector3( -0.4082482904639, 0.4082482904639, -0.4082482904639 * 2.0 );
	}
};

FaceTexture g_faceTextureClipboard;

void FaceTextureClipboard_setDefault(){
	g_faceTextureClipboard.m_flags = ContentsFlagsValue( 0, 0, 0, false );
	g_faceTextureClipboard.m_projection.m_texdef = texdef_t();
	g_faceTextureClipboard.m_projection.m_brushprimit_texdef = brushprimit_texdef_t();
	TexDef_Construct_Default( g_faceTextureClipboard.m_projection );
}

void TextureClipboard_textureSelected( const char* shader ){
	FaceTextureClipboard_setDefault();
}


class PatchEdgeIter
{
protected:
	const PatchControl* const m_ctrl;
	const int m_width;
	const int m_height;
	int m_row;
	int m_col;
	const PatchControl& ctrlAt( size_t row, size_t col ) const {
		return m_ctrl[row * m_width + col];
	}
public:
	PatchEdgeIter( const PatchData& patch ) : m_ctrl( patch.data() ), m_width( patch.getWidth() ), m_height( patch.getHeight() ){
	}
	PatchEdgeIter( const PatchEdgeIter& other ) = default;
	virtual ~PatchEdgeIter(){};
	virtual std::unique_ptr<PatchEdgeIter> clone() const = 0;
	const PatchControl& operator*() const {
		return ctrlAt( m_row, m_col );
	}
	operator bool() const {
		return m_row >=0 && m_row < m_height && m_col >=0 && m_col < m_width;
	}
	virtual void operator++() = 0;
	void operator+=( size_t inc ){
		while( inc-- )
			operator++();
	}
	std::unique_ptr<PatchEdgeIter> operator+( size_t inc ) const {
		std::unique_ptr<PatchEdgeIter> it = clone();
		*it += inc;
		return it;
	}
	virtual std::unique_ptr<PatchEdgeIter> getCrossIter() const = 0;
};

class PatchRowBackIter : public PatchEdgeIter
{
public:
	PatchRowBackIter( const PatchData& patch, size_t row ) : PatchEdgeIter( patch ){
		m_row = row;
		m_col = m_width - 1;
	}
	PatchRowBackIter( const PatchEdgeIter& base ) : PatchEdgeIter( base ){}
	std::unique_ptr<PatchEdgeIter> clone() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchRowBackIter( *this ) );
	}
	void operator++() override {
		--m_col;
	}
	std::unique_ptr<PatchEdgeIter> getCrossIter() const override;
};
class PatchRowForwardIter : public PatchEdgeIter
{
public:
	PatchRowForwardIter( const PatchData& patch, size_t row ) : PatchEdgeIter( patch ){
		m_row = row;
		m_col = 0;
	}
	PatchRowForwardIter( const PatchEdgeIter& base ) : PatchEdgeIter( base ){}
	std::unique_ptr<PatchEdgeIter> clone() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchRowForwardIter( *this ) );
	}
	void operator++() override {
		++m_col;
	}
	std::unique_ptr<PatchEdgeIter> getCrossIter() const override;
};
class PatchColBackIter : public PatchEdgeIter
{
public:
	PatchColBackIter( const PatchData& patch, size_t col ) : PatchEdgeIter( patch ){
		m_row = m_height - 1;
		m_col = col;
	}
	PatchColBackIter( const PatchEdgeIter& base ) : PatchEdgeIter( base ){}
	std::unique_ptr<PatchEdgeIter> clone() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchColBackIter( *this ) );
	}
	void operator++() override {
		--m_row;
	}
	std::unique_ptr<PatchEdgeIter> getCrossIter() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchRowBackIter( *this ) );
	}
};
class PatchColForwardIter : public PatchEdgeIter
{
public:
	PatchColForwardIter( const PatchData& patch, size_t col ) : PatchEdgeIter( patch ){
		m_row = 0;
		m_col = col;
	}
	PatchColForwardIter( const PatchEdgeIter& base ) : PatchEdgeIter( base ){}
	std::unique_ptr<PatchEdgeIter> clone() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchColForwardIter( *this ) );
	}
	void operator++() override {
		++m_row;
	}
	std::unique_ptr<PatchEdgeIter> getCrossIter() const override {
		return std::unique_ptr<PatchEdgeIter>( new PatchRowForwardIter( *this ) );
	}
};

std::unique_ptr<PatchEdgeIter> PatchRowBackIter::getCrossIter() const {
	return std::unique_ptr<PatchEdgeIter>( new PatchColForwardIter( *this ) );
}

std::unique_ptr<PatchEdgeIter> PatchRowForwardIter::getCrossIter() const {
	return std::unique_ptr<PatchEdgeIter>( new PatchColBackIter( *this ) );
}

// returns 0 or 3 CW points
static std::vector<const PatchControl*> Patch_getClosestTriangle( const PatchData& patch, const Winding& w ){
	/*
	// height = 3
	col  0  1  2  3  4
	    10 11 12 13 14  // row 2
	     5  6  7  8  9  // row 1
	     0  1  2  3  4  // row 0 // width = 5
	*/

	const auto triangle_ok = []( const PatchControl& p0, const PatchControl& p1, const PatchControl& p2 ){
		return vector3_length_squared( vector3_cross( p1.m_vertex - p0.m_vertex, p2.m_vertex - p0.m_vertex ) ) > 1.0;
	};

	const double eps = .25;

	const auto line_close = [eps]( const Line& line, const PatchControl& p ){
		return vector3_length_squared( line_closest_point( line, p.m_vertex ) - p.m_vertex ) < eps;
	};

	for ( std::size_t i = w.numpoints - 1, j = 0; j < w.numpoints; i = j, ++j ){
		const Line line( w[i].vertex, w[j].vertex );

		for( auto& iter : {
			std::unique_ptr<PatchEdgeIter>( new PatchRowBackIter( patch, 0 ) ),
			std::unique_ptr<PatchEdgeIter>( new PatchRowForwardIter( patch, patch.getHeight() - 1 ) ),
			std::unique_ptr<PatchEdgeIter>( new PatchColBackIter( patch, patch.getWidth() - 1 ) ),
			std::unique_ptr<PatchEdgeIter>( new PatchColForwardIter( patch, 0 ) ) } )
		{
			for( const std::unique_ptr<PatchEdgeIter>& i0 = iter; *i0; *i0 += 2 ){
				const PatchControl& p0 = **i0;
				if( line_close( line, p0 ) ){
					for( std::unique_ptr<PatchEdgeIter> i1 = *i0 + size_t{ 2 }; *i1; *i1 += 2 ){
						const PatchControl& p1 = **i1;
						if( line_close( line, p1 )
						 && vector3_length_squared( p1.m_vertex - p0.m_vertex ) > eps ){
							for( std::unique_ptr<PatchEdgeIter> i2 = *i0->getCrossIter() + size_t{ 1 }, i22 = *i1->getCrossIter() + size_t{ 1 }; *i2 && *i22; ++*i2, ++*i22 ){
								for( const PatchControl& p2 : { **i2, **i22 } ){
									if( triangle_ok( p0, p1, p2 ) ){
										return { &p0, &p1, &p2 };
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return {};
}


void Face_getTexture( Face& face, CopiedString& shader, FaceTexture& clipboard ){
	shader = face.GetShader();

	face.GetTexdef( clipboard.m_projection );
	clipboard.m_flags = face.getShader().m_flags;

	clipboard.m_plane = face.getPlane().plane3();
	clipboard.m_winding = face.getWinding();
	clipboard.m_width = face.getShader().width();
	clipboard.m_height = face.getShader().height();

	clipboard.m_colour = face.getShader().state()->getTexture().color;

	clipboard.m_pasteSource = FaceTexture::eBrush;
}
typedef Function3<Face&, CopiedString&, FaceTexture&, void, Face_getTexture> FaceGetTexture;

void Face_setTexture( Face& face, const char* shader, const FaceTexture& clipboard, EPasteMode mode, bool setShader ){
	if( setShader ){
		face.SetShader( shader );
		face.SetFlags( clipboard.m_flags );
	}
	if( mode == ePasteValues ){
		face.SetTexdef( clipboard.m_projection, false );
	}
	else if( mode == ePasteProject ){
		face.ProjectTexture( clipboard.m_projection, clipboard.m_plane.normal() );
	}
	else if( mode == ePasteSeamless ){
		if( clipboard.m_pasteSource == FaceTexture::eBrush ){
			DoubleRay line = plane3_intersect_plane3( clipboard.m_plane, face.getPlane().plane3() );
			if( vector3_length_squared( line.direction ) <= 1e-10 ){
				face.ProjectTexture( clipboard.m_projection, clipboard.m_plane.normal() );
				return;
			}

			const Quaternion rotation = quaternion_for_unit_vectors( clipboard.m_plane.normal(), face.getPlane().plane3().normal() );
//			globalOutputStream() << "rotation: " << rotation.x() << " " << rotation.y() << " " << rotation.z() << " " << rotation.w() << " " << "\n";
			Matrix4 transform = g_matrix4_identity;
			matrix4_pivoted_rotate_by_quaternion( transform, rotation, line.origin );

			TextureProjection proj = clipboard.m_projection;
			proj.m_brushprimit_texdef.addScale( clipboard.m_width, clipboard.m_height );
			Texdef_transformLocked( proj, clipboard.m_width, clipboard.m_height, clipboard.m_plane, transform, line.origin );
			proj.m_brushprimit_texdef.removeScale( clipboard.m_width, clipboard.m_height );

			face.SetTexdef( proj );

			CopiedString dummy;
			Face_getTexture( face, dummy, g_faceTextureClipboard );
		}
		else if( clipboard.m_pasteSource == FaceTexture::ePatch ){
			const auto pc = Patch_getClosestTriangle( clipboard.m_patch, face.getWinding() );
			// todo in patch->brush, brush->patch shall we apply texture, if alignment part fails?
			if( pc.empty() )
				return;
			DoubleVector3 vertices[3]{ pc[0]->m_vertex, pc[1]->m_vertex, pc[2]->m_vertex };
			const DoubleVector3 sts[3]{ DoubleVector3( pc[0]->m_texcoord ),
		                                DoubleVector3( pc[1]->m_texcoord ),
		                                DoubleVector3( pc[2]->m_texcoord ) };
			{ // rotate patch points to face plane
				const Plane3 plane = plane3_for_points( vertices );
				const DoubleRay line = plane3_intersect_plane3( face.getPlane().plane3(), plane );
				if( vector3_length_squared( line.direction ) > 1e-10 ){
					const Quaternion rotation = quaternion_for_unit_vectors( plane.normal(), face.getPlane().plane3().normal() );
					Matrix4 rot( g_matrix4_identity );
					matrix4_pivoted_rotate_by_quaternion( rot, rotation, line.origin );
					for( auto& v : vertices )
						matrix4_transform_point( rot, v );
				}
			}
			TextureProjection proj;
			Texdef_from_ST( proj, vertices, sts, clipboard.m_width, clipboard.m_height );
			proj.m_brushprimit_texdef.removeScale( clipboard.m_width, clipboard.m_height );
			face.SetTexdef( proj );

			CopiedString dummy;
			Face_getTexture( face, dummy, g_faceTextureClipboard );
		}

	}
}
typedef Function5<Face&, const char*, const FaceTexture&, EPasteMode, bool, void, Face_setTexture> FaceSetTexture;


void Patch_getTexture( Patch& patch, CopiedString& shader, FaceTexture& clipboard ){
	shader = patch.GetShader();
	FaceTextureClipboard_setDefault();

	clipboard.m_width = patch.getShader()->getTexture().width;
	clipboard.m_height = patch.getShader()->getTexture().height;

	clipboard.m_colour = patch.getShader()->getTexture().color;

	clipboard.m_patch.copy( patch );

	clipboard.m_pasteSource = FaceTexture::ePatch;
}
typedef Function3<Patch&, CopiedString&, FaceTexture&, void, Patch_getTexture> PatchGetTexture;

void Patch_setTexture( Patch& patch, const char* shader, const FaceTexture& clipboard, EPasteMode mode, bool setShader ){
	if( setShader )
		patch.SetShader( shader );
	if( mode == ePasteProject )
		patch.ProjectTexture( clipboard.m_projection, clipboard.m_plane.normal() );
	else if( mode == ePasteSeamless ){
		PatchData patchData;
		patchData.copy( patch );
		const auto pc = Patch_getClosestTriangle( patchData, clipboard.m_winding );

		if( pc.empty() )
			return;

		DoubleVector3 vertices[3]{ pc[0]->m_vertex, pc[1]->m_vertex, pc[2]->m_vertex };
		const DoubleVector3 sts[3]{ DoubleVector3( pc[0]->m_texcoord ),
		                            DoubleVector3( pc[1]->m_texcoord ),
		                            DoubleVector3( pc[2]->m_texcoord ) };
		Matrix4 local2tex0; // face tex projection
		{
			TextureProjection proj0( clipboard.m_projection );
			proj0.m_brushprimit_texdef.addScale( clipboard.m_width, clipboard.m_height );
			Texdef_Construct_local2tex( proj0, clipboard.m_width, clipboard.m_height, clipboard.m_plane.normal(), local2tex0 );
		}

		{ // rotate patch points to face plane
			const Plane3 plane = plane3_for_points( vertices );
			const DoubleRay line = plane3_intersect_plane3( clipboard.m_plane, plane );
			if( vector3_length_squared( line.direction ) > 1e-10 ){
				const Quaternion rotation = quaternion_for_unit_vectors( plane.normal(), clipboard.m_plane.normal() );
				Matrix4 rot( g_matrix4_identity );
				matrix4_pivoted_rotate_by_quaternion( rot, rotation, line.origin );
				for( auto& v : vertices )
					matrix4_transform_point( rot, v );
			}
		}

		Matrix4 local2tex; // patch BP tex projection
		Texdef_Construct_local2tex_from_ST( vertices, sts, local2tex );
		Matrix4 tex2local = matrix4_affine_inverse( local2tex );
		tex2local.t().vec3() += tex2local.z().vec3() * clipboard.m_plane.dist(); // adjust t() so that st->world points get to the plane

		const Matrix4 mat = matrix4_multiplied_by_matrix4( local2tex0, tex2local ); // unproject st->world, project to new st
		patch.undoSave();
		for( auto& p : patch ){
			p.m_texcoord = matrix4_transformed_point( mat, Vector3( p.m_texcoord ) ).vec2();
		}
		patch.controlPointsChanged();

		// Patch_getTexture
		g_faceTextureClipboard.m_width = patch.getShader()->getTexture().width;
		g_faceTextureClipboard.m_height = patch.getShader()->getTexture().height;
		g_faceTextureClipboard.m_colour = patch.getShader()->getTexture().color;
		g_faceTextureClipboard.m_patch.copy( patch );
		g_faceTextureClipboard.m_pasteSource = FaceTexture::ePatch;
	}
}
typedef Function5<Patch&, const char*, const FaceTexture&, EPasteMode, bool, void, Patch_setTexture> PatchSetTexture;

#include "ientity.h"
void Light_getTexture( Entity& entity, CopiedString& shader, FaceTexture& clipboard ){
	string_parse_vector3( entity.getKeyValue( "_color" ), clipboard.m_colour );
	if( !string_parse_float( entity.getKeyValue( "_light" ), clipboard.m_light ) )
		string_parse_float( entity.getKeyValue( "light" ), clipboard.m_light );
}
typedef Function3<Entity&, CopiedString&, FaceTexture&, void, Light_getTexture> LightGetTexture;

void Light_setTexture( Entity& entity, const char* shader, const FaceTexture& clipboard, EPasteMode mode, bool setShader ){
	if( mode == ePasteSeamless || mode == ePasteProject ){
		char value[64];
		sprintf( value, "%g %g %g", clipboard.m_colour[0], clipboard.m_colour[1], clipboard.m_colour[2] );
		entity.setKeyValue( "_color", value );
	}
	if( mode == ePasteValues || mode == ePasteProject ){
		/* copypaste of write_intensity() from entity plugin */
		char value[64];
		sprintf( value, "%g", clipboard.m_light );
		if( entity.hasKeyValue( "_light" ) ) //primaryIntensity //if set
			entity.setKeyValue( "_light", value );
		else //secondaryIntensity
			entity.setKeyValue( "light", value ); //otherwise default to "light", which is understood by both q3 and q1
	}
}
typedef Function5<Entity&, const char*, const FaceTexture&, EPasteMode, bool, void, Light_setTexture> LightSetTexture;


typedef Callback2<CopiedString&, FaceTexture&> GetTextureCallback;
typedef Callback4<const char*, const FaceTexture&, EPasteMode, bool, void> SetTextureCallback;

struct Texturable
{
	GetTextureCallback getTexture;
	SetTextureCallback setTexture;
};


void Face_getClosest( Face& face, SelectionTest& test, SelectionIntersection& bestIntersection, Texturable& texturable ){
	if ( face.isFiltered() ) {
		return;
	}
	SelectionIntersection intersection;
	face.testSelect( test, intersection );
	if ( intersection.valid()
	  && SelectionIntersection_closer( intersection, bestIntersection ) ) {
		bestIntersection = intersection;
		texturable.setTexture = makeCallback4( FaceSetTexture(), face );
		texturable.getTexture = makeCallback2( FaceGetTexture(), face );
	}
}


class OccludeSelector : public Selector
{
	SelectionIntersection& m_bestIntersection;
	bool& m_occluded;
public:
	OccludeSelector( SelectionIntersection& bestIntersection, bool& occluded ) : m_bestIntersection( bestIntersection ), m_occluded( occluded ){
		m_occluded = false;
	}
	void pushSelectable( Selectable& selectable ){
	}
	void popSelectable(){
	}
	void addIntersection( const SelectionIntersection& intersection ){
		if ( SelectionIntersection_closer( intersection, m_bestIntersection ) ) {
			m_bestIntersection = intersection;
			m_occluded = true;
		}
	}
};

class BrushGetClosestFaceVisibleWalker : public scene::Graph::Walker
{
	SelectionTest& m_test;
	Texturable& m_texturable;
	mutable SelectionIntersection m_bestIntersection;
public:
	BrushGetClosestFaceVisibleWalker( SelectionTest& test, Texturable& texturable ) : m_test( test ), m_texturable( texturable ){
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const {
		if ( !path.top().get().visible() )
			return false;
		BrushInstance* brush = Instance_getBrush( instance );
		if ( brush != 0 ) {
			m_test.BeginMesh( brush->localToWorld() );

			for ( Brush::const_iterator i = brush->getBrush().begin(); i != brush->getBrush().end(); ++i )
			{
				Face_getClosest( *( *i ), m_test, m_bestIntersection, m_texturable );
			}
		}
		else
		{
			SelectionTestable* selectionTestable = Instance_getSelectionTestable( instance );
			if ( selectionTestable ) {
				bool occluded;
				OccludeSelector selector( m_bestIntersection, occluded );
				selectionTestable->testSelect( selector, m_test );
				if ( occluded ) {
					Patch* patch = Node_getPatch( path.top() );
					if ( patch != 0 ) {
						m_texturable.setTexture = makeCallback4( PatchSetTexture(), *patch );
						m_texturable.getTexture = makeCallback2( PatchGetTexture(), *patch );
						return true;
					}
					Entity* entity = Node_getEntity( path.top() );
					if( entity != 0 && string_equal_n( entity->getClassName(), "light", 5 ) ){
						m_texturable.setTexture = makeCallback4( LightSetTexture(), *entity );
						m_texturable.getTexture = makeCallback2( LightGetTexture(), *entity );
					}
					else{
						m_texturable = Texturable();
					}
				}
			}
		}
		return true;
	}
};

Texturable Scene_getClosestTexturable( scene::Graph& graph, SelectionTest& test ){
	Texturable texturable;
	graph.traverse( BrushGetClosestFaceVisibleWalker( test, texturable ) );
	return texturable;
}

bool Scene_getClosestTexture( scene::Graph& graph, SelectionTest& test, CopiedString& shader, FaceTexture& clipboard ){
	Texturable texturable = Scene_getClosestTexturable( graph, test );
	if ( texturable.getTexture != GetTextureCallback() ) {
		texturable.getTexture( shader, clipboard );
		return true;
	}
	return false;
}

void Scene_setClosestTexture( scene::Graph& graph, SelectionTest& test, const char* shader, const FaceTexture& clipboard, EPasteMode mode, bool setShader ){
	Texturable texturable = Scene_getClosestTexturable( graph, test );
	if ( texturable.setTexture != SetTextureCallback() ) {
		texturable.setTexture( shader, clipboard, mode, setShader );
	}
}

void TextureBrowser_SetSelectedShader( const char* shader );
const char* TextureBrowser_GetSelectedShader();

void Scene_copyClosestTexture( SelectionTest& test ){
	CopiedString shader;
	if ( Scene_getClosestTexture( GlobalSceneGraph(), test, shader, g_faceTextureClipboard ) ) {
		TextureBrowser_SetSelectedShader( shader.c_str() );
	}
}

const char* Scene_applyClosestTexture_getUndoName( bool shift, bool ctrl, bool alt ){
	const EPasteMode mode = pastemode_for_modifiers( shift, ctrl );
	const bool setShader = pastemode_if_setShader( mode, alt );
	switch ( mode )
	{
	default: //case ePasteNone:
		return "paintTexture";
	case ePasteValues:
		return setShader? "paintTexture,Values,LightPower" : "paintTexDefValues";
	case ePasteSeamless:
		return setShader? "paintTextureSeamless,LightColor" : "paintTexDefValuesSeamless";
	case ePasteProject:
		return setShader? "projectTexture,LightColor&Power" : "projectTexDefValues";
	}
}

void Scene_applyClosestTexture( SelectionTest& test, bool shift, bool ctrl, bool alt, bool texturize_selected = false ){
//	UndoableCommand command( "facePaintTexture" );

	const EPasteMode mode = pastemode_for_modifiers( shift, ctrl );
	const bool setShader = pastemode_if_setShader( mode, alt );

	if( texturize_selected ){
		if( setShader && mode != ePasteSeamless )
			Select_SetShader( TextureBrowser_GetSelectedShader() );
		if( mode == ePasteValues )
			Select_SetTexdef( g_faceTextureClipboard.m_projection, false, false );
		else if( mode == ePasteProject )
			Select_ProjectTexture( g_faceTextureClipboard.m_projection, g_faceTextureClipboard.m_plane.normal() );
	}

	Scene_setClosestTexture( GlobalSceneGraph(), test, TextureBrowser_GetSelectedShader(), g_faceTextureClipboard, mode, setShader );

	SceneChangeNotify();
}





void SelectedFaces_copyTexture(){
	if ( !g_SelectedFaceInstances.empty() ) {
		Face& face = g_SelectedFaceInstances.last().getFace();
		face.GetTexdef( g_faceTextureClipboard.m_projection );
		g_faceTextureClipboard.m_flags = face.getShader().m_flags;

		TextureBrowser_SetSelectedShader( face.getShader().getShader() );
	}
}

void FaceInstance_pasteTexture( FaceInstance& faceInstance ){
	faceInstance.getFace().SetTexdef( g_faceTextureClipboard.m_projection );
	faceInstance.getFace().SetShader( TextureBrowser_GetSelectedShader() );
	faceInstance.getFace().SetFlags( g_faceTextureClipboard.m_flags );
	SceneChangeNotify();
}

bool SelectedFaces_empty(){
	return g_SelectedFaceInstances.empty();
}

void SelectedFaces_pasteTexture(){
	UndoableCommand command( "facePasteTexture" );
	g_SelectedFaceInstances.foreach( FaceInstance_pasteTexture );
}



void SurfaceInspector_constructPreferences( PreferencesPage& page ){
	page.appendCheckBox( "", "Surface Inspector Increments Match Grid", g_si_globals.m_bSnapTToGrid );
}
void SurfaceInspector_constructPage( PreferenceGroup& group ){
	PreferencesPage page( group.createPage( "Surface Inspector", "Surface Inspector Preferences" ) );
	SurfaceInspector_constructPreferences( page );
}
void SurfaceInspector_registerPreferencesPage(){
	PreferencesDialog_addSettingsPage( FreeCaller1<PreferenceGroup&, SurfaceInspector_constructPage>() );
}

void SurfaceInspector_registerCommands(){
	GlobalCommands_insert( "TextureReset/Cap", FreeCaller<SurfaceInspector_ResetTexture>(), Accelerator( 'N', GDK_SHIFT_MASK ) );
	GlobalCommands_insert( "FitTexture", FreeCaller<SurfaceInspector_FitTexture>(), Accelerator( 'F', GDK_CONTROL_MASK ) );
	GlobalCommands_insert( "FitTextureWidth", FreeCaller<SurfaceInspector_FaceFitWidth>() );
	GlobalCommands_insert( "FitTextureHeight", FreeCaller<SurfaceInspector_FaceFitHeight>() );
	GlobalCommands_insert( "FitTextureWidthOnly", FreeCaller<SurfaceInspector_FaceFitWidthOnly>() );
	GlobalCommands_insert( "FitTextureHeightOnly", FreeCaller<SurfaceInspector_FaceFitHeightOnly>() );
	GlobalCommands_insert( "TextureProjectAxial", FreeCaller<SurfaceInspector_ProjectTexture_eProjectAxial>() );
	GlobalCommands_insert( "TextureProjectOrtho", FreeCaller<SurfaceInspector_ProjectTexture_eProjectOrtho>() );
	GlobalCommands_insert( "TextureProjectCam", FreeCaller<SurfaceInspector_ProjectTexture_eProjectCam>() );
	GlobalCommands_insert( "SurfaceInspector", FreeCaller<SurfaceInspector_toggleShown>(), Accelerator( 'S' ) );

//	GlobalCommands_insert( "FaceCopyTexture", FreeCaller<SelectedFaces_copyTexture>() );
//	GlobalCommands_insert( "FacePasteTexture", FreeCaller<SelectedFaces_pasteTexture>() );
}


#include "preferencesystem.h"


void SurfaceInspector_Construct(){
	g_SurfaceInspector = new SurfaceInspector;

	SurfaceInspector_registerCommands();

	FaceTextureClipboard_setDefault();

	GlobalPreferenceSystem().registerPreference( "SurfaceWnd", getSurfaceInspector().m_importPosition, getSurfaceInspector().m_exportPosition );
	GlobalPreferenceSystem().registerPreference( "SI_SurfaceTexdef_Scale1", FloatImportStringCaller( g_si_globals.scale[0] ), FloatExportStringCaller( g_si_globals.scale[0] ) );
	GlobalPreferenceSystem().registerPreference( "SI_SurfaceTexdef_Scale2", FloatImportStringCaller( g_si_globals.scale[1] ), FloatExportStringCaller( g_si_globals.scale[1] ) );
	GlobalPreferenceSystem().registerPreference( "SI_SurfaceTexdef_Shift1", FloatImportStringCaller( g_si_globals.shift[0] ), FloatExportStringCaller( g_si_globals.shift[0] ) );
	GlobalPreferenceSystem().registerPreference( "SI_SurfaceTexdef_Shift2", FloatImportStringCaller( g_si_globals.shift[1] ), FloatExportStringCaller( g_si_globals.shift[1] ) );
	GlobalPreferenceSystem().registerPreference( "SI_SurfaceTexdef_Rotate", FloatImportStringCaller( g_si_globals.rotate ), FloatExportStringCaller( g_si_globals.rotate ) );
	GlobalPreferenceSystem().registerPreference( "SnapTToGrid", BoolImportStringCaller( g_si_globals.m_bSnapTToGrid ), BoolExportStringCaller( g_si_globals.m_bSnapTToGrid ) );

	typedef FreeCaller1<const Selectable&, SurfaceInspector_SelectionChanged> SurfaceInspectorSelectionChangedCaller;
	GlobalSelectionSystem().addSelectionChangeCallback( SurfaceInspectorSelectionChangedCaller() );
	typedef FreeCaller<SurfaceInspector_updateSelection> SurfaceInspectorUpdateSelectionCaller;
	Brush_addTextureChangedCallback( SurfaceInspectorUpdateSelectionCaller() );
	Patch_addTextureChangedCallback( SurfaceInspectorUpdateSelectionCaller() );

	SurfaceInspector_registerPreferencesPage();
}
void SurfaceInspector_Destroy(){
	delete g_SurfaceInspector;
}



#if TEXTOOL_ENABLED

namespace TexTool { // namespace hides these symbols from other object-files
//
//Shamus: Textool functions, including GTK+ callbacks
//

//NOTE: Black screen when TT first comes up is caused by an uninitialized Extent... !!! FIX !!!
//      But... You can see down below that it *is* initialized! WTF?
struct Extent
{
	float minX, minY, maxX, maxY;
	float width( void ) { return fabs( maxX - minX ); }
	float height( void ) { return fabs( maxY - minY ); }
};

//This seems to control the texture scale... (Yep! ;-)
Extent extents = { -2.0f, -2.0f, +2.0f, +2.0f };
brushprimit_texdef_t tm;                        // Texture transform matrix
Vector2 pts[c_brush_maxFaces];
Vector2 center;
int numPts;
int textureNum;
Vector2 textureSize;
Vector2 windowSize;
#define VP_PADDING  1.2
#define PI          3.14159265358979
bool lButtonDown = false;
bool rButtonDown = false;
//int dragPoint;
//int anchorPoint;
bool haveAnchor = false;
brushprimit_texdef_t currentBP;
brushprimit_texdef_t origBP;                    // Original brush primitive (before we muck it up)
float controlRadius = 5.0f;
float rotationAngle = 0.0f;
float rotationAngle2 = 0.0f;
float oldRotationAngle;
Vector2 rotationPoint;
bool translatingX = false;                      // Widget state variables
bool translatingY = false;
bool scalingX = false;
bool scalingY = false;
bool rotating = false;
bool resizingX = false;                         // Not sure what this means... :-/
bool resizingY = false;
float origAngle, origScaleX, origScaleY;
Vector2 oldCenter;


// Function prototypes (move up to top later...)

void DrawCircularArc( Vector2 ctr, float startAngle, float endAngle, float radius );


void CopyPointsFromSelectedFace( void ){
	// Make sure that there's a face and winding to get!

	if ( g_SelectedFaceInstances.empty() ) {
		numPts = 0;
		return;
	}

	Face & face = g_SelectedFaceInstances.last().getFace();
	textureNum = face.getShader().m_state->getTexture().texture_number;
	textureSize.x() = face.getShader().m_state->getTexture().width;
	textureSize.y() = face.getShader().m_state->getTexture().height;
//globalOutputStream() << "--> Texture #" << textureNum << ": " << textureSize.x() << " x " << textureSize.y() << "...\n";

	currentBP = SurfaceInspector_GetSelectedTexdef().m_brushprimit_texdef;

	face.EmitTextureCoordinates();
	Winding & w = face.getWinding();
	int count = 0;

	for ( Winding::const_iterator i = w.begin(); i != w.end(); i++ )
	{
		//globalOutputStream() << (*i).texcoord.x() << " " << (*i).texcoord.y() << ", ";
		pts[count].x() = ( *i ).texcoord.x();
		pts[count].y() = ( *i ).texcoord.y();
		count++;
	}

	numPts = count;

	//globalOutputStream() << " ..copied points\n";
}

brushprimit_texdef_t bp;
//This approach is probably wrongheaded and just not right anyway. So, !!! FIX !!! [DONE]
void CommitChanges( void ){
	texdef_t t;                                 // Throwaway, since this is BP only

	bp.coords[0][0] = tm.coords[0][0] * origBP.coords[0][0] + tm.coords[0][1] * origBP.coords[1][0];
	bp.coords[0][1] = tm.coords[0][0] * origBP.coords[0][1] + tm.coords[0][1] * origBP.coords[1][1];
	bp.coords[0][2] = tm.coords[0][0] * origBP.coords[0][2] + tm.coords[0][1] * origBP.coords[1][2] + tm.coords[0][2];
//Ok, this works for translation...
//	bp.coords[0][2] = tm.coords[0][0] * origBP.coords[0][2] + tm.coords[0][1] * origBP.coords[1][2] + tm.coords[0][2] * textureSize.x();
	bp.coords[1][0] = tm.coords[1][0] * origBP.coords[0][0] + tm.coords[1][1] * origBP.coords[1][0];
	bp.coords[1][1] = tm.coords[1][0] * origBP.coords[0][1] + tm.coords[1][1] * origBP.coords[1][1];
	bp.coords[1][2] = tm.coords[1][0] * origBP.coords[0][2] + tm.coords[1][1] * origBP.coords[1][2] + tm.coords[1][2];
//	bp.coords[1][2] = tm.coords[1][0] * origBP.coords[0][2] + tm.coords[1][1] * origBP.coords[1][2] + tm.coords[1][2] * textureSize.y();

//This doesn't work:	g_brush_texture_changed();
// Let's try this:
//Note: We should only set an undo *after* the button has been released... !!! FIX !!!
//Definitely *should* have an undo, though!
//  UndoableCommand undo("textureProjectionSetSelected");
	Select_SetTexdef( TextureProjection( t, bp, Vector3( 0, 0, 0 ), Vector3( 0, 0, 0 ) ) );
//This is working, but for some reason the translate is causing the rest of the SI
//widgets to yield bad readings... !!! FIX !!!
//I.e., click on textool window, translate face wireframe, then controls go crazy. Dunno why.
//It's because there were some uncommented out add/removeScale functions in brush.h and a
//removeScale in brushmanip.cpp... :-/
//Translate isn't working at all now... :-(
//It's because we need to multiply in some scaling factor (prolly the texture width/height)
//Yep. :-P
}

void UpdateControlPoints( void ){
	CommitChanges();

	// Init texture transform matrix

	tm.coords[0][0] = 1.0f; tm.coords[0][1] = 0.0f; tm.coords[0][2] = 0.0f;
	tm.coords[1][0] = 0.0f; tm.coords[1][1] = 1.0f; tm.coords[1][2] = 0.0f;
}


/*
   For shifting we have:
 */
/*
   The code that should provide reasonable defaults, but doesn't for some reason:
   It's scaling the BP by 128 for some reason, between the time it's created and the
   time we get back to the SI widgets:

   static void OnBtnAxial(GtkWidget *widget, gpointer data)
   {
   UndoableCommand undo("textureDefault");
   TextureProjection projection;
   TexDef_Construct_Default(projection);
   Select_SetTexdef(projection);
   }

   Select_SetTexdef() calls Scene_BrushSetTexdef_Component_Selected(GlobalSceneGraph(), projection)
   which is in brushmanip.h: This eventually calls
   Texdef_Assign(m_texdef, texdef, m_brushprimit_texdef, brushprimit_texdef) in class Face...
   which just copies from brushpr to m_brushpr...
 */

//Small problem with this thing: It's scaled to the texture which is all screwed up... !!! FIX !!! [DONE]
//Prolly should separate out the grid drawing so that we can draw it behind the polygon.
const float gridWidth = 1.3f; // Let's try an absolute height... WORKS!!!
// NOTE that 2.0 is the height of the viewport. Dunno why... Should make collision
//      detection easier...
const float gridRadius = gridWidth * 0.5f;

typedef const float WidgetColor[3];
const WidgetColor widgetColor[10] = {
	{ 1.0000f, 0.2000f, 0.0000f },          // Red
	{ 0.9137f, 0.9765f, 0.4980f },          // Yellow
	{ 0.0000f, 0.6000f, 0.3216f },          // Green
	{ 0.6157f, 0.7726f, 0.8196f },          // Cyan
	{ 0.4980f, 0.5000f, 0.4716f },          // Grey

	// Highlight colors
	{ 1.0000f, 0.6000f, 0.4000f },          // Light Red
	{ 1.0000f, 1.0000f, 0.8980f },          // Light Yellow
	{ 0.4000f, 1.0000f, 0.7216f },          // Light Green
	{ 1.0000f, 1.0000f, 1.0000f },          // Light Cyan
	{ 0.8980f, 0.9000f, 0.8716f }           // Light Grey
};

#define COLOR_RED           0
#define COLOR_YELLOW        1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_GREY          4
#define COLOR_LT_RED        5
#define COLOR_LT_YELLOW     6
#define COLOR_LT_GREEN      7
#define COLOR_LT_CYAN       8
#define COLOR_LT_GREY       9

void DrawControlWidgets( void ){
//Note that the grid should go *behind* the face outline... !!! FIX !!!
	// Grid
	float xStart = center.x() - ( gridWidth / 2.0f );
	float yStart = center.y() - ( gridWidth / 2.0f );
	float xScale = ( extents.height() / extents.width() ) * ( textureSize.y() / textureSize.x() );

	glPushMatrix();
//Small problem with this approach: Changing the center point in the TX code doesn't seem to
//change anything here--prolly because we load a new identity matrix. A couple of ways to fix
//this would be to get rid of that code, or change the center to a new point by taking into
//account the transforms that we toss with the new identity matrix. Dunno which is better.
	glLoadIdentity();
	glScalef( xScale, 1.0, 1.0 );           // Will that square it up? Yup.
	glRotatef( static_cast<float>( radians_to_degrees( atan2( -currentBP.coords[0][1], currentBP.coords[0][0] ) ) ), 0.0, 0.0, -1.0 );
	glTranslatef( -center.x(), -center.y(), 0.0 );

	// Circle
	glColor3fv( translatingX && translatingY ? widgetColor[COLOR_LT_YELLOW] : widgetColor[COLOR_YELLOW] );
	glBegin( GL_LINE_LOOP );
	DrawCircularArc( center, 0, 2.0f * PI, gridRadius * 0.16 );

	glEnd();

	// Axes
	glBegin( GL_LINES );
	glColor3fv( translatingY && !translatingX ? widgetColor[COLOR_LT_GREEN] : widgetColor[COLOR_GREEN] );
	glVertex2f( center.x(), center.y() + ( gridRadius * 0.16 ) );
	glVertex2f( center.x(), center.y() + ( gridRadius * 1.00 ) );
	glColor3fv( translatingX && !translatingY ? widgetColor[COLOR_LT_RED] : widgetColor[COLOR_RED] );
	glVertex2f( center.x() + ( gridRadius * 0.16 ), center.y() );
	glVertex2f( center.x() + ( gridRadius * 1.00 ), center.y() );
	glEnd();

	// Arrowheads
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glBegin( GL_TRIANGLES );
	glColor3fv( translatingY && !translatingX ? widgetColor[COLOR_LT_GREEN] : widgetColor[COLOR_GREEN] );
	glVertex2f( center.x(), center.y() + ( gridRadius * 1.10 ) );
	glVertex2f( center.x() + ( gridRadius * 0.06 ), center.y() + ( gridRadius * 0.94 ) );
	glVertex2f( center.x() - ( gridRadius * 0.06 ), center.y() + ( gridRadius * 0.94 ) );
	glColor3fv( translatingX && !translatingY ? widgetColor[COLOR_LT_RED] : widgetColor[COLOR_RED] );
	glVertex2f( center.x() + ( gridRadius * 1.10 ), center.y() );
	glVertex2f( center.x() + ( gridRadius * 0.94 ), center.y() + ( gridRadius * 0.06 ) );
	glVertex2f( center.x() + ( gridRadius * 0.94 ), center.y() - ( gridRadius * 0.06 ) );
	glEnd();

	// Arc
	glBegin( GL_LINE_STRIP );
	glColor3fv( rotating ? widgetColor[COLOR_LT_CYAN] : widgetColor[COLOR_CYAN] );
	DrawCircularArc( center, 0.03f * PI, 0.47f * PI, gridRadius * 0.90 );
	glEnd();

	// Boxes
	glColor3fv( scalingY && !scalingX ? widgetColor[COLOR_LT_GREEN] : widgetColor[COLOR_GREEN] );
	glBegin( GL_LINES );
	glVertex2f( center.x() + ( gridRadius * 0.20 ), center.y() + ( gridRadius * 1.50 ) );
	glVertex2f( center.x() - ( gridRadius * 0.20 ), center.y() + ( gridRadius * 1.50 ) );
	glEnd();
	glBegin( GL_LINE_LOOP );
	glVertex2f( center.x() + ( gridRadius * 0.10 ), center.y() + ( gridRadius * 1.40 ) );
	glVertex2f( center.x() - ( gridRadius * 0.10 ), center.y() + ( gridRadius * 1.40 ) );
	glVertex2f( center.x() - ( gridRadius * 0.10 ), center.y() + ( gridRadius * 1.20 ) );
	glVertex2f( center.x() + ( gridRadius * 0.10 ), center.y() + ( gridRadius * 1.20 ) );
	glEnd();

	glColor3fv( scalingX && !scalingY ? widgetColor[COLOR_LT_RED] : widgetColor[COLOR_RED] );
	glBegin( GL_LINES );
	glVertex2f( center.x() + ( gridRadius * 1.50 ), center.y() + ( gridRadius * 0.20 ) );
	glVertex2f( center.x() + ( gridRadius * 1.50 ), center.y() - ( gridRadius * 0.20 ) );
	glEnd();
	glBegin( GL_LINE_LOOP );
	glVertex2f( center.x() + ( gridRadius * 1.40 ), center.y() + ( gridRadius * 0.10 ) );
	glVertex2f( center.x() + ( gridRadius * 1.40 ), center.y() - ( gridRadius * 0.10 ) );
	glVertex2f( center.x() + ( gridRadius * 1.20 ), center.y() - ( gridRadius * 0.10 ) );
	glVertex2f( center.x() + ( gridRadius * 1.20 ), center.y() + ( gridRadius * 0.10 ) );
	glEnd();

	glColor3fv( scalingX && scalingY ? widgetColor[COLOR_LT_CYAN] : widgetColor[COLOR_CYAN] );
	glBegin( GL_LINE_STRIP );
	glVertex2f( center.x() + ( gridRadius * 1.50 ), center.y() + ( gridRadius * 1.10 ) );
	glVertex2f( center.x() + ( gridRadius * 1.50 ), center.y() + ( gridRadius * 1.50 ) );
	glVertex2f( center.x() + ( gridRadius * 1.10 ), center.y() + ( gridRadius * 1.50 ) );
	glEnd();
	glBegin( GL_LINE_LOOP );
	glVertex2f( center.x() + ( gridRadius * 1.40 ), center.y() + ( gridRadius * 1.40 ) );
	glVertex2f( center.x() + ( gridRadius * 1.40 ), center.y() + ( gridRadius * 1.20 ) );
	glVertex2f( center.x() + ( gridRadius * 1.20 ), center.y() + ( gridRadius * 1.20 ) );
	glVertex2f( center.x() + ( gridRadius * 1.20 ), center.y() + ( gridRadius * 1.40 ) );
	glEnd();

	glPopMatrix();
}

void DrawControlPoints( void ){
	glColor3f( 1, 1, 1 );
	glBegin( GL_LINE_LOOP );

	for ( int i = 0; i < numPts; i++ )
		glVertex2f( pts[i].x(), pts[i].y() );

	glEnd();
}

// Note: Setup and all that jazz must be done by the caller!

void DrawCircularArc( Vector2 ctr, float startAngle, float endAngle, float radius ){
	float stepSize = ( 2.0f * PI ) / 200.0f;

	for ( float angle = startAngle; angle <= endAngle; angle += stepSize )
		glVertex2f( ctr.x() + radius * cos( angle ), ctr.y() + radius * sin( angle ) );
}


void focus(){
	if ( numPts == 0 ) {
		return;
	}

	// Find selected texture's extents...

	extents.minX = extents.maxX = pts[0].x(),
	extents.minY = extents.maxY = pts[0].y();

	for ( int i = 1; i < numPts; i++ )
	{
		if ( pts[i].x() < extents.minX ) {
			extents.minX = pts[i].x();
		}
		if ( pts[i].x() > extents.maxX ) {
			extents.maxX = pts[i].x();
		}
		if ( pts[i].y() < extents.minY ) {
			extents.minY = pts[i].y();
		}
		if ( pts[i].y() > extents.maxY ) {
			extents.maxY = pts[i].y();
		}
	}

	// Do some viewport fitting stuff...

//globalOutputStream() << "--> Center: " << center.x() << ", " << center.y() << "\n";
//globalOutputStream() << "--> Extents (stage 1): " << extents.minX << ", "
//	<< extents.maxX << ", " << extents.minY << ", " << extents.maxY << "\n";
	// TTimo: Apply a ratio to get the area we'll draw.
	center.x() = 0.5f * ( extents.minX + extents.maxX ),
	center.y() = 0.5f * ( extents.minY + extents.maxY );
	extents.minX = center.x() + VP_PADDING * ( extents.minX - center.x() ),
	extents.minY = center.y() + VP_PADDING * ( extents.minY - center.y() ),
	extents.maxX = center.x() + VP_PADDING * ( extents.maxX - center.x() ),
	extents.maxY = center.y() + VP_PADDING * ( extents.maxY - center.y() );
//globalOutputStream() << "--> Extents (stage 2): " << extents.minX << ", "
//	<< extents.maxX << ", " << extents.minY << ", " << extents.maxY << "\n";

	// TTimo: We want a texture with the same X / Y ratio.
	// TTimo: Compute XY space / window size ratio.
	float SSize = extents.width(), TSize = extents.height();
	float ratioX = textureSize.x() * extents.width() / windowSize.x(),
	      ratioY = textureSize.y() * extents.height() / windowSize.y();
//globalOutputStream() << "--> Texture size: " << textureSize.x() << ", " << textureSize.y() << "\n";
//globalOutputStream() << "--> Window size: " << windowSize.x() << ", " << windowSize.y() << "\n";

	if ( ratioX > ratioY ) {
		TSize = ( windowSize.y() * ratioX ) / textureSize.y();
//		TSize = extents.width() * (windowSize.y() / windowSize.x()) * (textureSize.x() / textureSize.y());
	}
	else
	{
		SSize = ( windowSize.x() * ratioY ) / textureSize.x();
//		SSize = extents.height() * (windowSize.x() / windowSize.y()) * (textureSize.y() / textureSize.x());
	}

	extents.minX = center.x() - 0.5f * SSize, extents.maxX = center.x() + 0.5f * SSize,
	extents.minY = center.y() - 0.5f * TSize, extents.maxY = center.y() + 0.5f * TSize;
//globalOutputStream() << "--> Extents (stage 3): " << extents.minX << ", "
//	<< extents.maxX << ", " << extents.minY << ", " << extents.maxY << "\n";
}

gboolean size_allocate( GtkWidget * win, GtkAllocation * a, gpointer ){
	windowSize.x() = a->width;
	windowSize.y() = a->height;
	queueDraw();
	return false;
}

gboolean expose( GtkWidget * win, GdkEventExpose * e, gpointer ){
//	globalOutputStream() << "--> Textool Window was exposed!\n";
//	globalOutputStream() << "    (window width/height: " << cc << "/" << e->area.height << ")\n";

//	windowSize.x() = e->area.width, windowSize.y() = e->area.height;
//This needs to go elsewhere...
//	InitTextool();

	if ( !glwidget_make_current( win ) ) {
		globalOutputStream() << "    FAILED to make current! Oh, the agony! :-(\n";
		return true;
	}

	CopyPointsFromSelectedFace();

	if ( !lButtonDown ) {
		focus();
	}

	// Probably should init button/anchor states here as well...
//	rotationAngle = 0.0f;
	glClearColor( 0, 0, 0, 0 );
	glViewport( 0, 0, e->area.width, e->area.height );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

//???
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );

	glOrtho( extents.minX, extents.maxX, extents.maxY, extents.minY, -1, 1 );

	glColor3f( 1, 1, 1 );
	// draw the texture background
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glBindTexture( GL_TEXTURE_2D, textureNum );

	glEnable( GL_TEXTURE_2D );
	glBegin( GL_QUADS );
	glTexCoord2f( extents.minX, extents.minY );
	glVertex2f( extents.minX, extents.minY );
	glTexCoord2f( extents.maxX, extents.minY );
	glVertex2f( extents.maxX, extents.minY );
	glTexCoord2f( extents.maxX, extents.maxY );
	glVertex2f( extents.maxX, extents.maxY );
	glTexCoord2f( extents.minX, extents.maxY );
	glVertex2f( extents.minX, extents.maxY );
	glEnd();
	glDisable( GL_TEXTURE_2D );

	// draw the texture-space grid
	glColor3fv( widgetColor[COLOR_GREY] );
	glBegin( GL_LINES );

	const int gridSubdivisions = 8;
	const float gridExtents = 4.0f;

	for ( int i = 0; i < gridSubdivisions + 1; ++i )
	{
		float y = i * ( gridExtents / float(gridSubdivisions) );
		float x = i * ( gridExtents / float(gridSubdivisions) );
		glVertex2f( 0, y );
		glVertex2f( gridExtents, y );
		glVertex2f( x, 0 );
		glVertex2f( x, gridExtents );
	}

	glEnd();

	DrawControlPoints();
	DrawControlWidgets();
//???
	// reset the current texture
//  glBindTexture(GL_TEXTURE_2D, 0);
//  glFinish();
	glwidget_swap_buffers( win );

	return false;
}

/*int FindSelectedPoint(int x, int y)
   {
    for(int i=0; i<numPts; i++)
    {
        int nx = (int)(windowSize.x() * (pts[i].x() - extents.minX) / extents.width());
        int ny = (int)(windowSize.y() * (pts[i].y() - extents.minY) / extents.height());

        if (abs(nx - x) <= 3 && abs(ny - y) <= 3)
            return i;
    }

    return -1;
   }//*/

Vector2 trans;
Vector2 trans2;
Vector2 dragPoint;  // Defined in terms of window space (+x/-y)
Vector2 oldTrans;
gboolean button_press( GtkWidget * win, GdkEventButton * e, gpointer ){
//	globalOutputStream() << "--> Textool button press...\n";

	if ( e->button == 1 ) {
		lButtonDown = true;
		GlobalUndoSystem().start();

		origBP = currentBP;

		//globalOutputStream() << "--> Original BP: [" << origBP.coords[0][0] << "][" << origBP.coords[0][1] << "][" << origBP.coords[0][2] << "]\n";
		//globalOutputStream() << "                 [" << origBP.coords[1][0] << "][" << origBP.coords[1][1] << "][" << origBP.coords[1][2] << "]\n";
		//float angle = atan2(origBP.coords[0][1], origBP.coords[0][0]) * 180.0f / 3.141592653589f;
		origAngle = ( origBP.coords[0][1] > 0 ? PI : -PI ); // Could also be -PI... !!! FIX !!! [DONE]

		if ( origBP.coords[0][0] != 0.0f ) {
			origAngle = atan( origBP.coords[0][1] / origBP.coords[0][0] );
		}

		origScaleX = origBP.coords[0][0] / cos( origAngle );
		origScaleY = origBP.coords[1][1] / cos( origAngle );
		rotationAngle = origAngle;
		oldCenter[0] = oldCenter[1] = 0;

		//globalOutputStream() << "--> BP stats: ang=" << origAngle * RAD_TO_DEG << ", scale=" << origScaleX << "/" << origScaleY << "\n";
		//Should also set the Flip X/Y checkboxes here as well... !!! FIX !!!
		//Also: should reverse texture left/right up/down instead of flipping the points...

//disnowok
//float nx = windowSize.x() * (e->x - extents.minX) / (extents.maxX - extents.minX);
//float ny = windowSize.y() * (e->y - extents.minY) / (extents.maxY - extents.minY);
//disdoes...
//But I want it to scroll the texture window, not the points... !!! FIX !!!
//Actually, should scroll the texture window only when mouse is down on no widgets...
		float nx = e->x / windowSize.x() * extents.width() + extents.minX;
		float ny = e->y / windowSize.y() * extents.height() + extents.minY;
		trans.x() = -tm.coords[0][0] * nx - tm.coords[0][1] * ny;
		trans.y() = -tm.coords[1][0] * nx - tm.coords[1][1] * ny;

		dragPoint.x() = e->x, dragPoint.y() = e->y;
		trans2.x() = nx, trans2.y() = ny;
		oldRotationAngle = rotationAngle;
//		oldTrans.x() = tm.coords[0][2] - nx * textureSize.x();
//		oldTrans.y() = tm.coords[1][2] - ny * textureSize.y();
		oldTrans.x() = tm.coords[0][2];
		oldTrans.y() = tm.coords[1][2];
		oldCenter.x() = center.x();
		oldCenter.y() = center.y();

		queueDraw();

		return true;
	}
/*	else if (e->button == 3)
    {
        rButtonDown = true;
    }//*/

//globalOutputStream() << "(" << (haveAnchor ? "anchor" : "released") << ")\n";

	return false;
}

gboolean button_release( GtkWidget * win, GdkEventButton * e, gpointer ){
//	globalOutputStream() << "--> Textool button release...\n";

	if ( e->button == 1 ) {
/*		float ptx = e->x / windowSize.x() * extents.width() + extents.minX;
        float pty = e->y / windowSize.y() * extents.height() + extents.minY;

   //This prolly should go into the mouse move code...
   //Doesn't work correctly anyway...
        if (translatingX || translatingY)
            center.x() = ptx, center.y() = pty;//*/

		lButtonDown = false;

		if ( translatingX || translatingY ) {
			GlobalUndoSystem().finish( "translateTexture" );
		}
		else if ( rotating ) {
			GlobalUndoSystem().finish( "rotateTexture" );
		}
		else if ( scalingX || scalingY ) {
			GlobalUndoSystem().finish( "scaleTexture" );
		}
		else if ( resizingX || resizingY ) {
			GlobalUndoSystem().finish( "resizeTexture" );
		}
		else
		{
			GlobalUndoSystem().finish( "textoolUnknown" );
		}

		rotating = translatingX = translatingY = scalingX = scalingY = resizingX = resizingY = false;

		queueDraw();
	}
	else if ( e->button == 3 ) {
		rButtonDown = false;
	}

	return true;
}

/*
   void C2DView::GridForWindow( float c[2], int x, int y)
   {
   SpaceForWindow( c, x, y );
   if ( !m_bDoGrid )
    return;
   c[0] /= m_GridStep[0];
   c[1] /= m_GridStep[1];
   c[0] = (float)floor( c[0] + 0.5f );
   c[1] = (float)floor( c[1] + 0.5f );
   c[0] *= m_GridStep[0];
   c[1] *= m_GridStep[1];
   }
   void C2DView::SpaceForWindow( float c[2], int x, int y)
   {
   c[0] = ((float)(x))/((float)(m_rect.right-m_rect.left))*(m_Maxs[0]-m_Mins[0])+m_Mins[0];
   c[1] = ((float)(y))/((float)(m_rect.bottom-m_rect.top))*(m_Maxs[1]-m_Mins[1])+m_Mins[1];
   }
 */
gboolean motion( GtkWidget * win, GdkEventMotion * e, gpointer ){
//	globalOutputStream() << "--> Textool motion...\n";

	if ( lButtonDown ) {
		if ( translatingX || translatingY ) {
			float ptx = e->x / windowSize.x() * extents.width() + extents.minX;
			float pty = e->y / windowSize.y() * extents.height() + extents.minY;

//Need to fix this to take the rotation angle into account, so that it moves along
//the rotated X/Y axis...
			if ( translatingX ) {
//				tm.coords[0][2] = (trans.x() + ptx) * textureSize.x();
//This works, but only when the angle is zero. !!! FIX !!! [DONE]
//				tm.coords[0][2] = oldCenter.x() + (ptx * textureSize.x());
				tm.coords[0][2] = oldTrans.x() + ( ptx - trans2.x() ) * textureSize.x();
//				center.x() = oldCenter.x() + (ptx - trans2.x());
			}

			if ( translatingY ) {
//				tm.coords[1][2] = (trans.y() + pty) * textureSize.y();
//				tm.coords[1][2] = oldCenter.y() + (pty * textureSize.y());
				tm.coords[1][2] = oldTrans.y() + ( pty - trans2.y() ) * textureSize.y();
//				center.y() = oldCenter.y() + (pty - trans2.y());
			}

//Need to update center.x/y() so that the widget translates as well. Also, oldCenter
//is badly named... Should be oldTrans or something like that... !!! FIX !!!
//Changing center.x/y() here doesn't seem to change anything... :-/
			UpdateControlPoints();
		}
		else if ( rotating ) {
			// Shamus: New rotate code
			int cx = (int)( windowSize.x() * ( center.x() - extents.minX ) / extents.width() );
			int cy = (int)( windowSize.y() * ( center.y() - extents.minY ) / extents.height() );
			Vector3 v1( dragPoint.x() - cx, dragPoint.y() - cy, 0 ), v2( e->x - cx, e->y - cy, 0 );

			vector3_normalise( v1 );
			vector3_normalise( v2 );
			float c = vector3_dot( v1, v2 );
			Vector3 cross = vector3_cross( v1, v2 );
			float s = vector3_length( cross );

			if ( cross[2] > 0 ) {
				s = -s;
			}

// Problem with this: arcsin/cos seems to only return -90 to 90 and 0 to 180...
// Can't derive angle from that!

//rotationAngle = asin(s);// * 180.0f / 3.141592653589f;
			rotationAngle = acos( c );
//rotationAngle2 = asin(s);
			if ( cross[2] < 0 ) {
				rotationAngle = -rotationAngle;
			}

//NO! DOESN'T WORK! rotationAngle -= 45.0f * DEG_TO_RAD;
//Let's try this:
//No wok.
/*c = cos(rotationAngle - oldRotationAngle);
   s = sin(rotationAngle - oldRotationAngle);
   rotationAngle += oldRotationAngle;
   //c += cos(oldRotationAngle);
   //s += sin(oldRotationAngle);
   //rotationAngle += oldRotationAngle;
   //c %= 2.0 * PI;
   //s %= 2.0 * PI;
   //rotationAngle %= 2.0 * PI;//*/

//This is wrong... Hmm...
//It seems to shear the texture instead of rotating it... !!! FIX !!!
// Now it rotates correctly. Seems TTimo was overcomplicating things here... ;-)

// Seems like what needs to happen here is multiplying these rotations by tm... !!! FIX !!!

// See brush_primit.cpp line 244 (Texdef_EmitTextureCoordinates()) for where texcoords come from...

			tm.coords[0][0] =  c;
			tm.coords[0][1] =  s;
			tm.coords[1][0] = -s;
			tm.coords[1][1] =  c;
//It doesn't work anymore... Dunno why...
//tm.coords[0][2] = -trans.x();			// This works!!! Yeah!!!
//tm.coords[1][2] = -trans.y();
//nope.
//tm.coords[0][2] = rotationPoint.x();	// This works, but strangely...
//tm.coords[1][2] = rotationPoint.y();
//tm.coords[0][2] = 0;// center.x() / 2.0f;
//tm.coords[1][2] = 0;// center.y() / 2.0f;
//No.
//tm.coords[0][2] = -(center.x() * textureSize.x());
//tm.coords[1][2] = -(center.y() * textureSize.y());
//Eh? No, but seems to be getting closer...
/*float ptx = e->x / windowSize.x() * extents.width() + extents.minX;
   float pty = e->y / windowSize.y() * extents.height() + extents.minY;
   tm.coords[0][2] = -c * center.x() - s * center.y() + ptx;
   tm.coords[1][2] =  s * center.x() - c * center.x() + pty;//*/
//Kinda works, but center drifts around on non-square textures...
/*tm.coords[0][2] = (-c * center.x() - s * center.y()) * textureSize.x();
   tm.coords[1][2] = ( s * center.x() - c * center.y()) * textureSize.y();//*/
//Rotates correctly, but not around the actual center of the face's points...
/*tm.coords[0][2] = -c * center.x() * textureSize.x() - s * center.y() * textureSize.y();
   tm.coords[1][2] =  s * center.x() * textureSize.x() - c * center.y() * textureSize.y();//*/
//Yes!!!
			tm.coords[0][2] = ( -c * center.x() * textureSize.x() - s * center.y() * textureSize.y() ) + center.x() * textureSize.x();
			tm.coords[1][2] = ( s * center.x() * textureSize.x() - c * center.y() * textureSize.y() ) + center.y() * textureSize.y(); //*/
//This doesn't work...
//And this is the wrong place for this anyway (I'm pretty sure).
/*tm.coords[0][2] += oldCenter.x();
   tm.coords[1][2] += oldCenter.y();//*/
			UpdateControlPoints(); // will cause a redraw
		}

		return true;
	}
	else                                    // Check for widget mouseovers
	{
		Vector2 tran;
		float nx = e->x / windowSize.x() * extents.width() + extents.minX;
		float ny = e->y / windowSize.y() * extents.height() + extents.minY;
		// Translate nx/y to the "center" point...
		nx -= center.x();
		ny -= center.y();
		ny = -ny;   // Flip Y-axis so that increasing numbers move up

		tran.x() = tm.coords[0][0] * nx + tm.coords[0][1] * ny;
		tran.y() = tm.coords[1][0] * nx + tm.coords[1][1] * ny;
//This doesn't seem to generate a valid distance from the center--for some reason it
//calculates a fixed number every time
//Look at nx/y above: they're getting fixed there! !!! FIX !!! [DONE]
		float dist = sqrt( ( nx * nx ) + ( ny * ny ) );
		// Normalize to the 2.0 = height standard (for now)
//globalOutputStream() << "--> Distance before: " << dist;
		dist = dist * 2.0f / extents.height();
//globalOutputStream() << ". After: " << dist;
		tran.x() = tran.x() * 2.0f / extents.height();
		tran.y() = tran.y() * 2.0f / extents.height();
//globalOutputStream() << ". Trans: " << tran.x() << ", " << tran.y() << "\n";

//Let's try this instead...
//Interesting! It seems that e->x/y are rotated
//(no, they're not--the TM above is what's doing it...)
		nx = ( ( e->x / windowSize.y() ) * 2.0f ) - ( windowSize.x() / windowSize.y() );
		ny = ( ( e->y / windowSize.y() ) * 2.0f ) - ( windowSize.y() / windowSize.y() );
		ny = -ny;
//Cool! It works! Now just need to do rotation...

		rotating = translatingX = translatingY = scalingX = scalingY = resizingX = resizingY = false;

		if ( dist < ( gridRadius * 0.16f ) ) {
			translatingX = translatingY = true;
		}
		else if ( dist > ( gridRadius * 0.16f ) && dist < ( gridRadius * 1.10f )
		          && fabs( ny ) < ( gridRadius * 0.05f ) && nx > 0 ) {
			translatingX = true;
		}
		else if ( dist > ( gridRadius * 0.16f ) && dist < ( gridRadius * 1.10f )
		          && fabs( nx ) < ( gridRadius * 0.05f ) && ny > 0 ) {
			translatingY = true;
		}
		// Should tighten up the angle on this, or put this test after the axis tests...
		else if ( tran.x() > 0 && tran.y() > 0
		          && ( dist > ( gridRadius * 0.82f ) && dist < ( gridRadius * 0.98f ) ) ) {
			rotating = true;
		}

		queueDraw();

		return true;
	}

	return false;
}

//It seems the fake tex coords conversion is screwing this stuff up... !!! FIX !!!
//This is still wrong... Prolly need to do something with the oldScaleX/Y stuff...
void flipX( GtkToggleButton *, gpointer ){
//	globalOutputStream() << "--> Flip X...\n";
	//Shamus:
//	SurfaceInspector_GetSelectedBPTexdef();		// Refresh g_selectedBrushPrimitTexdef...
//	tm.coords[0][0] = -tm.coords[0][0];
//	tm.coords[1][0] = -tm.coords[1][0];
//	tm.coords[0][0] = -tm.coords[0][0];			// This should be correct now...Nope.
//	tm.coords[1][1] = -tm.coords[1][1];
	tm.coords[0][0] = -tm.coords[0][0];         // This should be correct now...
	tm.coords[1][0] = -tm.coords[1][0];
//	tm.coords[2][0] = -tm.coords[2][0];//wil wok? no.
	UpdateControlPoints();
}

void flipY( GtkToggleButton *, gpointer ){
//	globalOutputStream() << "--> Flip Y...\n";
//	tm.coords[0][1] = -tm.coords[0][1];
//	tm.coords[1][1] = -tm.coords[1][1];
//	tm.coords[0][1] = -tm.coords[0][1];			// This should be correct now...Nope.
//	tm.coords[1][0] = -tm.coords[1][0];
	tm.coords[0][1] = -tm.coords[0][1];         // This should be correct now...
	tm.coords[1][1] = -tm.coords[1][1];
//	tm.coords[2][1] = -tm.coords[2][1];//wil wok? no.
	UpdateControlPoints();
}

} // end namespace TexTool

#endif
