



#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <X11/XKBlib.h>

#include "config.h"
#include "eggtrayicon.h"
#include "version.h"

static gchar version[] = VERSION;


//#define DEBUG
#include "dbg.h"

/******************************************************************
 * TYPEDEFS                                                        *
 ******************************************************************/
typedef struct _kbd_info {
    gchar *sym;
    gchar *name;
    GdkPixbuf *flag;
} kbd_info;

#define IMGPREFIX PREFIX "/share/fbxkb/images/"
/******************************************************************
 * GLOBAL VARSIABLES                                              *
 ******************************************************************/

/* X11 common stuff */
static Atom a_XKB_RULES_NAMES;
static Display *dpy;
static int xkb_event_type;

/* internal state mashine */
static int cur_group;
static int ngroups;
static GHashTable *sym2pix;
static kbd_info group2info[XkbNumKbdGroups];
static GdkPixbuf *zzflag;
static int active;
/* gtk gui */
static GtkWidget *flag_menu;
static GtkWidget *app_menu;
static GtkWidget *docklet;
static GtkWidget *image;
static GtkWidget *about_dialog = NULL;
/******************************************************************
 * DECLARATION                                                    *
 ******************************************************************/

static int init();
static void read_kbd_description();
static void update_flag(int no);
static GdkFilterReturn filter( XEvent *xev, GdkEvent *event, gpointer data);
static void Xerror_handler(Display * d, XErrorEvent * ev);
static GdkPixbuf *sym2flag(char *sym);
static void flag_menu_create();
static void flag_menu_destroy();
static void flag_menu_activated(GtkWidget *widget, gpointer data);
static void app_menu_create();
static void app_menu_about(GtkWidget *widget, gpointer data);
static void app_menu_exit(GtkWidget *widget, gpointer data);

static int docklet_create();

static int create_all();

/******************************************************************
 * CODE                                                           *
 ******************************************************************/

/******************************************************************
 * gtk gui                                                        *
 ******************************************************************/
static void
flag_menu_create()
{
    int i;
    GdkPixbuf *flag;
    GtkWidget *mi, *img;
    //static GString *s = NULL;;
    
    ENTER;
    flag_menu =  gtk_menu_new();
    for (i = 0; i < ngroups; i++) {
        mi = gtk_image_menu_item_new_with_label(
            group2info[i].name ? group2info[i].name : group2info[i].sym);
        g_signal_connect(G_OBJECT(mi), "activate", (GCallback)flag_menu_activated, GINT_TO_POINTER(i));
        gtk_menu_shell_append (GTK_MENU_SHELL (flag_menu), mi);
        gtk_widget_show (mi);
        flag = sym2flag(group2info[i].sym);
        img = gtk_image_new_from_pixbuf(flag);
        gtk_widget_show(img);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), img);
    }
    RET();
}

static void
flag_menu_destroy()
{
    if (flag_menu) {
        gtk_widget_destroy(flag_menu);
        flag_menu = NULL;
    }
}

static void
flag_menu_activated(GtkWidget *widget, gpointer data)
{
    int i;

    ENTER;    
    i = GPOINTER_TO_INT(data);
    DBG("asking %d group\n", i);
    XkbLockGroup(dpy, XkbUseCoreKbd, i);
    RET();
}

static void
app_menu_create()
{
    GtkWidget *mi;
    
    ENTER;
    app_menu =  gtk_menu_new();

    mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_DIALOG_INFO, NULL);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback)app_menu_about, NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (app_menu), mi);
    gtk_widget_show (mi);

    
    mi = gtk_menu_item_new ();
    gtk_widget_show (mi);
    gtk_menu_shell_append (GTK_MENU_SHELL (app_menu), mi);
    gtk_widget_set_sensitive (mi, FALSE);

    mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback)app_menu_exit, NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (app_menu), mi);
    gtk_widget_show (mi);
    RET();

} 

static void
app_menu_about(GtkWidget *widget, gpointer data)
{
    ENTER;
    if (!about_dialog) {
        about_dialog = gtk_message_dialog_new (NULL,
              GTK_DIALOG_DESTROY_WITH_PARENT,
              GTK_MESSAGE_INFO,
              GTK_BUTTONS_CLOSE,
              "fbxkb %s\nX11 Keyboard switcher\nAuthor: Anatoly Asviyan <aanatoly@users.sf.net>", version);
        /* Destroy the dialog when the user responds to it (e.g. clicks a button) */
        g_signal_connect_swapped (about_dialog, "response",
              G_CALLBACK (gtk_widget_hide),
              about_dialog);
    }
    gtk_widget_show (about_dialog);
    RET();
}


static void
app_menu_exit(GtkWidget *widget, gpointer data)
{
    ENTER;    
    exit(0);
    RET();
}


static void docklet_embedded(GtkWidget *widget, void *data) 
{
    ENTER;
    RET();
}

static void docklet_destroyed(GtkWidget *widget, void *data) 
{
    ENTER;
    //g_object_unref(G_OBJECT(docklet));
    docklet = NULL;
    g_idle_add(create_all, NULL);
    RET();
}


void docklet_clicked(GtkWidget *button, GdkEventButton *event, void *data) 
{
    //GtkWidget *menu;
    ENTER;
    if (event->type != GDK_BUTTON_PRESS)
        RET();

    if (event->button == 1) {
        int no;

        no =  (cur_group + 1) % ngroups;
        DBG("no=%d\n", no);
        XkbLockGroup(dpy, XkbUseCoreKbd, no);
    } else if (event->button == 2) {
        gtk_menu_popup(GTK_MENU(flag_menu), NULL, NULL, NULL, NULL, event->button, event->time);
    } else if (event->button == 3) {
        gtk_menu_popup(GTK_MENU(app_menu), NULL, NULL, NULL, NULL, event->button, event->time);
    }
    RET();
}

static int 
docklet_create() 
{
    GtkWidget *box;
    
    ENTER;
    docklet = (GtkWidget*)egg_tray_icon_new("fbxkb");
    box     = gtk_event_box_new();
    image   = gtk_image_new(); 
    //image   = gtk_image_new();
    g_signal_connect(G_OBJECT(docklet), "embedded", G_CALLBACK(docklet_embedded), NULL);
    g_signal_connect(G_OBJECT(docklet), "destroy", G_CALLBACK(docklet_destroyed), NULL);
    g_signal_connect(G_OBJECT(box), "button-press-event", G_CALLBACK(docklet_clicked), NULL);

    gtk_container_set_border_width(GTK_CONTAINER(box), 0);
    
    gtk_container_add(GTK_CONTAINER(box), image);
    gtk_container_add(GTK_CONTAINER(docklet), box);
    gtk_widget_show_all(GTK_WIDGET(docklet));
    
    RET(1);
}


/******************************************************************
 * internal state machine                                         *
 ******************************************************************/
static gboolean
my_str_equal (gchar *a, gchar *b)
{
    return  (a[0] == b[0] && a[1] == b[1]);
}

    
static GdkPixbuf *
sym2flag(char *sym)
{
    GdkPixbuf *flag;
    static GString *s = NULL;
    char tmp[3];
    
    ENTER;
    g_assert(sym != NULL && strlen(sym) > 1);
    flag = g_hash_table_lookup(sym2pix, sym);
    if (flag)
        RET(flag);

    if (!s) 
        s = g_string_new(IMGPREFIX "tt.png");
    s->str[s->len-6] = sym[0];
    s->str[s->len-5] = sym[1];
    flag = gdk_pixbuf_new_from_file_at_size(s->str, 24, 24, NULL);
    if (!flag)
        RET(zzflag);
    tmp[0] = sym[0];
    tmp[1] = sym[1];
    tmp[2] = 0;
    g_hash_table_insert(sym2pix, tmp, flag);
    RET(flag);
}


static void
read_kbd_description()
{
    unsigned int mask;
    XkbDescRec *kbd_desc_ptr;
    XkbStateRec xkb_state;
    Atom sym_name_atom;
    int i;

    ENTER;
    // clean up
    cur_group = ngroups = 0;
    for (i = 0; i < XkbNumKbdGroups; i++) {
        g_free(group2info[i].sym);
        g_free(group2info[i].name);
        /*
          if (group2info[i].flag)
            g_object_unref(G_OBJECT(group2info[i].flag));
        */
    }
    bzero(group2info, sizeof(group2info));

    // get kbd info
    mask = XkbControlsMask | XkbServerMapMask;
    kbd_desc_ptr = XkbAllocKeyboard();
    if (!kbd_desc_ptr) {
        ERR("can't alloc kbd info\n");
        goto out_us;
    }
    kbd_desc_ptr->dpy = dpy;
    if (XkbGetControls(dpy, XkbAllControlsMask, kbd_desc_ptr) != Success) {
        ERR("can't get Xkb controls\n");
        goto out;
    }
    ngroups = kbd_desc_ptr->ctrls->num_groups;
    if (ngroups < 1)
        goto out;
    if (XkbGetState(dpy, XkbUseCoreKbd, &xkb_state) != Success) {
        ERR("can't get Xkb state\n");
        goto out;
    }
    cur_group = xkb_state.group;
    DBG("cur_group = %d ngroups = %d\n", cur_group, ngroups);
    g_assert(cur_group < ngroups);
    
    if (XkbGetNames(dpy, XkbSymbolsNameMask, kbd_desc_ptr) != Success) {
        ERR("can't get Xkb symbol description\n");
        goto out;
    }
    if (XkbGetNames(dpy, XkbGroupNamesMask, kbd_desc_ptr) != Success)
        ERR("Failed to get keyboard description\n");
    g_assert(kbd_desc_ptr->names);
    sym_name_atom = kbd_desc_ptr->names->symbols;

    // parse kbd info
    if (sym_name_atom != None) {
        char *sym_name, *tmp, *tok;
        int no;
        
        sym_name = XGetAtomName(dpy, sym_name_atom);
        if (!sym_name)
            goto out;
        /* to know how sym_name might look like do this:
         *    % xlsatoms | grep pc
         *    150 pc/pc(pc101)+pc/us+pc/ru(phonetic):2+group(shift_toggle)
         *    470 pc(pc105)+us+ru(phonetic):2+il(phonetic):3+group(shifts_toggle)+group(switch)
         */
        DBG("sym_name=%s\n", sym_name);
        for (tok = strtok(sym_name, "+"); tok; tok = strtok(NULL, "+")) {
            DBG("tok=%s\n", tok);
            tmp = strchr(tok, ':');
            if (tmp) {
                if (sscanf(tmp+1, "%d", &no) != 1) 
                    ERR("can't read kbd number\n");
                no--;
                *tmp = 0;
            } else {
                no = 0;
            }
            for (tmp = tok; isalpha(*tmp); tmp++);
            *tmp = 0;

            DBG("map=%s no=%d\n", tok, no);
            if (!strcmp(tok, "pc") || !strcmp(tok, "group"))
                continue;
          
            g_assert((no >= 0) && (no < ngroups));
            if (group2info[no].sym != NULL) {
                //-jbb ERR("xkb group #%d is already defined\n", no);
            }
            else {
                group2info[no].sym = g_strdup(tok);
                group2info[no].flag = sym2flag(tok);
                group2info[no].name = XGetAtomName(dpy, kbd_desc_ptr->names->groups[no]);           
                
                //-jbb: for debugging: printf(" sym=%s\n", tok);
            }
        }
        XFree(sym_name);
    }
 out:
    XkbFreeKeyboard(kbd_desc_ptr, 0, True);
    // sanity check: group numbering must be continous
    for (i = 0; (i < XkbNumKbdGroups) && (group2info[i].sym != NULL); i++);
    if (i != ngroups) {
        ERR("kbd group numbering is not continous\n");
        ERR("run 'xlsatoms | grep pc' to know what hapends\n");
        exit(1);
    }
 out_us:
    //if no groups were defined just add default 'us' kbd group
    if (!ngroups) {
        ngroups = 1;
        cur_group = 0;
        group2info[0].sym = g_strdup("us");
        group2info[0].flag = sym2flag("us");
        group2info[0].name = NULL;
        ERR("no kbd groups defined. adding default 'us' group\n");
    }
    RET();
}



static void update_flag(int no)
{
    kbd_info *k = &group2info[no];
    ENTER;
    g_assert(k != NULL);
    DBG("k->sym=%s\n", k->sym);
    gtk_image_set_from_pixbuf(GTK_IMAGE(image), k->flag);
    RET();
}



static GdkFilterReturn
filter( XEvent *xev, GdkEvent *event, gpointer data)
{
    ENTER;
    if (!active)
        RET(GDK_FILTER_CONTINUE);
   
    if (xev->type ==  xkb_event_type) {
        XkbEvent *xkbev = (XkbEvent *) xev;
        DBG("XkbTypeEvent %d \n", xkbev->any.xkb_type);
        if (xkbev->any.xkb_type == XkbStateNotify) {
            DBG("XkbStateNotify: %d\n", xkbev->state.group);
            cur_group = xkbev->state.group;
            if (cur_group < ngroups)
                update_flag(cur_group);
        } else if (xkbev->any.xkb_type == XkbNewKeyboardNotify) {         
            DBG("XkbNewKeyboardNotify\n");
            read_kbd_description();
            //cur_group = 0;
            update_flag(cur_group);
            flag_menu_destroy();
            flag_menu_create();  
        }
        RET(GDK_FILTER_REMOVE);
    }
    RET(GDK_FILTER_CONTINUE);
}

static int
init()
{
    int dummy;

    ENTER;
    sym2pix  = g_hash_table_new(g_str_hash, (GEqualFunc) my_str_equal);
    dpy = gdk_x11_get_default_xdisplay();
    a_XKB_RULES_NAMES = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
    if (a_XKB_RULES_NAMES == None)
        ERR("_XKB_RULES_NAMES - can't get this atom\n");

    if (!XkbQueryExtension(dpy, &dummy, &xkb_event_type, &dummy, &dummy, &dummy))
        RET(0);
    DBG("xkb_event_type=%d\n", xkb_event_type);
    XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
          XkbAllStateComponentsMask, XkbGroupStateMask);
    gdk_window_add_filter(NULL, (GdkFilterFunc)filter, NULL);
    zzflag = gdk_pixbuf_new_from_file_at_size(IMGPREFIX "zz.png", 24, 24, NULL);
    RET(1);
}

#if 0


static void
app_menu_destroy()
{
    ENTER;
    if (app_menu) {
        gtk_widget_destroy(app_menu);
        app_menu = NULL;
    }
    RET();
}

static void
destroy_all()
{
    active = 0;
    gdk_window_remove_filter(NULL, (GdkFilterFunc)filter, NULL);
    XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
          XkbAllStateComponentsMask, 0UL);
    flag_menu_destroy();
    app_menu_destroy();
}
#endif

static int
create_all()
{
    ENTER;
    read_kbd_description();
    docklet_create();
    flag_menu_create();
    app_menu_create();
    update_flag(cur_group);
    active = 1;
    RET(FALSE);// FALSE will remove us from idle func
}

int
main(int argc, char *argv[], char *env[])
{
    ENTER;
    setlocale(LC_CTYPE, "");
    gtk_set_locale();
    gtk_init(&argc, &argv);
    XSetLocaleModifiers("");
    XSetErrorHandler((XErrorHandler) Xerror_handler);

    if (!init())
        ERR("can't init. exiting\n");
    create_all();
    gtk_main ();
    RET(0);
}


/********************************************************************/

void
Xerror_handler(Display * d, XErrorEvent * ev)
{
    char buf[256];

    ENTER;
    XGetErrorText(gdk_x11_get_default_xdisplay(), ev->error_code, buf, 256);
    ERR( "fbxkb : X error: %s\n", buf);
    RET();
}
