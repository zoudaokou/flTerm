//
// "$Id: Fl_Term.cxx 34192 2019-04-10 10:08:20 $"
//
// Fl_Term -- A terminal simulator widget
//
// Copyright 2017-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//
#include "ssh2.h"
#include "Fl_Term.h"
#include <FL/Fl_Menu.H>
#include <FL/filename.H>               // needed for fl_decode_rui
#include <thread>

int move_editor(int x, int y, int w, int h);
void sleep_ms( int ms );
void Fl_Term::host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	term->host_cb2(buf, len);
}
void Fl_Term::host_cb2(const char *buf, int len)
{
	if ( len>0 ) {
		if ( host->type()==HOST_CONF ) 
			putxml(buf, len);
		else
			puts(buf, len);
	}
	else {
		if ( len==0 ) {	//Connected or Disconnected
			disp("\033[33m"); disp(buf); 
			disp("\033[37m");
			if ( *buf=='C' ) 
				host->send_size(size_x, size_y);
			if ( *buf=='D' ) 
				disp(", press Enter to restart\n\n");
		}
		else {	//len<0
			disp("\033[31m"); disp(buf);
			disp(" failure\033[37m\n\n");
		}
	}
}

Fl_Menu_Item rclick_menu[]={{"Copy"}, {"Paste"},{"Select all"},{"Erase"},{0}};
Fl_Term::Fl_Term(int X,int Y,int W,int H,const char *L) : Fl_Widget(X,Y,W,H,L)
{
	bInsert = bAlterScreen = bAppCursor = false;
	bEscape = bGraphic = bTitle = false;
	bCursor = true;
	bEcho = false;
	bMouseScroll = false;
	ESC_idx = 0;
	host = NULL;

	color(FL_BLACK);
	textsize(16);
	strcpy(sTitle, "Term");
	strcpy(sPrompt, "> ");
	iPrompt = 2;
	iTimeOut = 30;
	bPrompt = true;
	bDND = false;
	fpLogFile = NULL;
	
	line = NULL;
	line_size = 0;
	buff = attr = NULL;
	buff_size = 0;
	redraw_complete = false;
	buffsize(4096);
	clear();
	for ( int i=0; i<4; i++ ) rclick_menu[i].labelsize(16);
}
Fl_Term::~Fl_Term()
{
	if ( host!=NULL ) delete host;
	free(attr);
	free(buff);
	free(line);
};
void Fl_Term::clear()
{
	Fl::lock();
	if ( line!=NULL ) memset(line, 0, line_size*sizeof(int) );
	if ( buff!=NULL ) memset(buff, 0, buff_size);
	if ( attr!=NULL ) memset(attr, 0, buff_size);
	cursor_y = cursor_x = 0;
	screen_y = scroll_y = 0;
	sel_left = sel_right= 0;
	c_attr = 7;				//default black background, white foreground
	recv0 = 0;
	
	xmlIndent=0;
	xmlTagIsOpen=true;

	Fl::unlock();
}
void Fl_Term::resize( int X, int Y, int W, int H )
{
	Fl_Widget::resize(X,Y,W,H);
	size_x = w()/iFontWidth;
	size_y = (h()-4)/iFontHeight;
	roll_top = 0;
	roll_bot = size_y-1;
	if ( !bAlterScreen ) {
		screen_y = cursor_y-size_y+1;
		if ( screen_y<0 ) screen_y = 0;
		scroll_y = 0;
	}
	redraw();
	if ( host!=NULL ) host->send_size(size_x, size_y);
}
void Fl_Term::textsize( int pt )
{
	if ( pt!=0 ) font_size = pt;
	fl_font(FL_COURIER, font_size);
	iFontWidth = fl_width('a');
	iFontHeight = fl_height();
	resize(x(), y(), w(), h());
}

const unsigned int VT_attr[8] = {	
	FL_BLACK, FL_RED, FL_GREEN, FL_YELLOW,	//0,1,2,3
	FL_BLUE, FL_MAGENTA, FL_CYAN, FL_WHITE	//4,5,6,7
};
void Fl_Term::draw()
{
	redraw_complete = true;
	fl_color(color());
	fl_rectf(x(),y(),w(),h());
	fl_font(FL_COURIER, font_size);

	int sel_l = sel_left;
	int sel_r = sel_right;
	if ( sel_l>sel_r ) {
		sel_l = sel_right;
		sel_r = sel_left;
	}

	int ly = screen_y+scroll_y;
	if ( ly<0 ) ly=0;
	int dx, dy = y();
	for ( int i=0; i<size_y; i++ ) {
		dx = x()+1;
		dy += iFontHeight;
		int j = line[ly+i];
		while( j<line[ly+i+1] ) {
			int n=j;
			while (  attr[n]==attr[j] ) {
				if ( ++n==line[ly+i+1]) break;
				if ( n==sel_r || n==sel_l ) break;
			}
			unsigned int font_color = VT_attr[(int)attr[j]&7];
			unsigned int bg_color = VT_attr[(int)((attr[j]&0x70)>>4)];
			int wi = fl_width(buff+j, n-j);
			if ( j>=sel_l && j<sel_r ) {
				fl_color(selection_color());
				fl_rectf(dx, dy-iFontHeight+4, wi, iFontHeight);
				fl_color(fl_contrast(font_color, selection_color()));
			}
			else {
				if ( bg_color!=color() ) {
					fl_color( bg_color );
					fl_rectf(dx, dy-iFontHeight+4, wi, iFontHeight);
				}
				fl_color( font_color );
			}
			fl_draw( buff+j, n-j, dx, dy );
			dx += wi;
			j=n;
		}
	}
	dx = x()+fl_width(buff+line[cursor_y], cursor_x-line[cursor_y]);
	dy = y()+(cursor_y-screen_y)*iFontHeight;
	if ( bCursor && active() ) {
		if ( !move_editor(dx+1, dy+iFontHeight/4, w()-dx-2, iFontHeight) ) {
			fl_color(FL_WHITE);		//draw a white bar as cursor
			fl_rectf(dx+1, dy+iFontHeight, 8, 4);
		}
	}
	if ( scroll_y || bMouseScroll) {
		fl_color(FL_DARK3);			//draw scrollbar
		fl_rectf(x()+w()-8, y(), 8, y()+h());
		fl_color(FL_RED);			//draw slider
		int slider_y = h()*(cursor_y+scroll_y)/cursor_y;
		fl_rectf(x()+w()-8, y()+slider_y-8, 8, 16);
	}
}
int Fl_Term::handle( int e ) {
	switch (e) {
		case FL_FOCUS: redraw(); return 1;
		case FL_MOUSEWHEEL:
			if ( !bAlterScreen ) {
				scroll_y += Fl::event_dy();
				if ( scroll_y<-screen_y ) scroll_y = -screen_y;
				if ( scroll_y>0 ) scroll_y = 0;
				redraw();
			}
			return 1;
		case FL_PUSH:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x=Fl::event_x()/iFontWidth;
				int y=Fl::event_y()-Fl_Widget::y();
				if ( Fl::event_clicks()==1 ) {	//double click to select word
					y = y/iFontHeight + screen_y+scroll_y;
					sel_left = line[y]+x;
					sel_right = sel_left;
					while ( --sel_left>line[y] )
						if ( buff[sel_left]==0x0a || buff[sel_left]==0x20 ) {
							sel_left++;
							break;
						}
					while ( ++sel_right<line[y+1])
						if ( buff[sel_right]==0x0a || buff[sel_right]==0x20 ) {
							break;
						}
					redraw();
					return 1;
				}
				if ( x>=size_x-2 && scroll_y!=0 ) {//push in scrollbar area
					bMouseScroll = true;
					scroll_y = y*cursor_y/h()-cursor_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					redraw();
				}
				else {								//push to start draging
					y = y/iFontHeight + screen_y+scroll_y;
					sel_left = line[y]+x;
					if ( sel_left>line[y+1] ) sel_left=line[y+1] ;
					while ( (buff[sel_left]&0xc0)==0x80 ) sel_left--;
					sel_right = sel_left;
				}
			}
			return 1;
		case FL_DRAG:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x = Fl::event_x()/iFontWidth;
				int y = Fl::event_y()-Fl_Widget::y();
				if ( bMouseScroll) {
					scroll_y = y*cursor_y/h()-cursor_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					if ( scroll_y>0 ) scroll_y=0;
				}
				else {
					if ( y<0 ) {
						scroll_y += y/8;
						if ( scroll_y<-cursor_y ) scroll_y = -cursor_y;
					}
					if ( y>h() ) {
						scroll_y += (y-h())/8;
						if ( scroll_y>0 ) scroll_y=0;
					}
					y = y/iFontHeight + screen_y+scroll_y;
					if ( y<0 ) y=0;
					if ( !bAlterScreen && y>cursor_y ) y = cursor_y;
					//cursor_y may not be the last line in AlterScreen mode
					sel_right = line[y]+x;
					if ( sel_right>line[y+1] ) sel_right=line[y+1];
					while ( (buff[sel_right]&0xc0)==0x80 ) sel_right++;
				}
				redraw();
			}
			return 1;
		case FL_RELEASE:
			switch ( Fl::event_button() ) {
			case FL_LEFT_MOUSE:		//left button drag to copy
				bMouseScroll = false;
				if ( sel_left>sel_right ) {
					int t=sel_left; sel_left=sel_right; sel_right=t;
				}
				break;
			case FL_MIDDLE_MOUSE:	//middle click to paste from selection
				write(buff+sel_left, sel_right-sel_left);
				break;
			case FL_RIGHT_MOUSE: {	//right click for context menu
					int ex = Fl::event_x(), ey=Fl::event_y();
					const Fl_Menu_Item *m = rclick_menu->popup( ex, ey );
					if ( m ) {
						const char *sel = m->label();
						switch ( *sel ) {
						case 'C': if ( sel_left<sel_right )
									Fl::copy( buff+sel_left, 
												sel_right-sel_left, 1);
								  break;
						case 'P': Fl::paste( *this, 1 ); break;
						case 'S': sel_left=0; sel_right=cursor_x; redraw();
								  break;
						case 'E': clear(); redraw();
						}
					}
				}
				break;
			}
			return 1;
		case FL_DND_RELEASE: bDND = true;
		case FL_DND_ENTER:
		case FL_DND_DRAG:
		case FL_DND_LEAVE:  return 1;
		case FL_PASTE:
			if ( bDND ) 
				run_script(strdup(Fl::event_text()));
			else
				write(Fl::event_text(),Fl::event_length());
			bDND = false;
			return 1;
		case FL_KEYDOWN:
			if ( Fl::event_state(FL_ALT)==0 ) {
#ifdef __APPLE__
				int del;
				if ( Fl::compose(del) ) {
					int y = (cursor_y-screen_y+1)*iFontHeight;
					int x = (cursor_x-line[cursor_y])*iFontWidth;
					Fl::insertion_point_location(x,y,iFontHeight);
					for ( int i=0; i<del; i++ ) write("\177",1);
				}
#endif
				int key = Fl::event_key();
				switch (key) {
				case FL_Page_Up:
					if ( !bAlterScreen ) {
						scroll_y-=size_y;
						if ( scroll_y<-screen_y ) scroll_y=-screen_y;
						redraw();
					}
					break;
				case FL_Page_Down:
					if ( !bAlterScreen ) {
						scroll_y+=size_y;
						if ( scroll_y>0 ) scroll_y = 0;
						redraw();
					}
					break;
				case FL_Up:	  write(bAppCursor?"\033OA":"\033[A",3); break;
				case FL_Down: write(bAppCursor?"\033OB":"\033[B",3); break;
				case FL_Right:write(bAppCursor?"\033OC":"\033[C",3); break;
				case FL_Left: write(bAppCursor?"\033OD":"\033[D",3); break;
				case FL_BackSpace: write("\177", 1); break;
				case FL_Enter:
				default:
					write(Fl::event_text(), Fl::event_length());
					scroll_y = 0;
				}
				return 1;
			}
	}
	return Fl_Widget::handle(e);
}
void Fl_Term::buffsize(int new_line_size)
{
	new_line_size += 512;
	if ( new_line_size == line_size ) return;

	int new_buff_size = new_line_size*64;
	char *old_buff = buff;
	char *old_attr = attr;
	int *old_line = line;
	
	Fl::lock();
	buff = (char *)realloc(buff, new_buff_size);
	attr = (char *)realloc(attr, new_buff_size);
	line = (int * )realloc(line, new_line_size*sizeof(int));
	if ( buff!=NULL && attr!=NULL && line!=NULL ) {
		if ( new_line_size>line_size ) {
			memset(line+line_size, 0, (new_line_size-line_size)*sizeof(int));
			memset(buff+buff_size, 0, new_buff_size-buff_size);
			memset(attr+buff_size, 0, new_buff_size-buff_size);
			buff_size = new_buff_size;
			line_size = new_line_size;
		}
		else {
			buff_size = new_buff_size;
			line_size = new_line_size;
			clear();
		}
	}
	else {					//clear buffer if failed to double
		if ( attr==NULL ) attr = old_attr;
		if ( buff==NULL ) buff = old_buff;
		if ( line==NULL ) line = old_line;
	}
	Fl::unlock();
}
void Fl_Term::next_line()
{
	line[++cursor_y]=cursor_x;
	if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
	
	if ( cursor_x>=buff_size-1024 || cursor_y==line_size-2 ) {
		Fl::lock();
		int i, len = line[1024];
		recv0 -= len;
		cursor_x -= len;
		cursor_y -= 1024;
		memmove(buff, buff+len, buff_size-len);
		memset(buff+cursor_x, 0, buff_size-cursor_x);
		memmove(attr, attr+len, buff_size-len);
		memset(attr+cursor_x, 0, buff_size-cursor_x);
		for ( i=0; i<cursor_y+2; i++ ) line[i] = line[i+1024]-len;
		while ( i<line_size ) line[i++]=0;
		screen_y -= 1024;
		scroll_y = 0;
		Fl::unlock();
	}
	if ( scroll_y<0 ) scroll_y--;
	if ( screen_y<cursor_y-size_y+1 ) screen_y = cursor_y-size_y+1;
}
void Fl_Term::append( const char *newtext, int len ){
	const unsigned char *p = (const unsigned char *)newtext;
	const unsigned char *zz = p+len;

	if ( fpLogFile!=NULL ) fwrite( newtext, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p );
	while ( p < zz ) {
		unsigned char c=*p++;
		if ( bTitle ) {
			if ( c==0x07 ) {
				bTitle = false;
				sTitle[title_idx]=0;
			}
			else
				sTitle[title_idx++] = c;
			continue;
		}
		switch ( c ) {
		case 	0: 	break;
		case 0x07: 	fprintf(stdout, "\007"); fflush(stdout); break;
		case 0x08:  if ( (buff[cursor_x--]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x--;
					break;
		case 0x09: 	do { 
						attr[cursor_x]=c_attr;
						buff[cursor_x++]=' '; 
					} while ( (cursor_x-line[cursor_y])%8!=0 );
					break;
		case 0x0a:	if ( bAlterScreen ) { 	//LF on alter screen is not stored
						if ( cursor_y==screen_y+roll_bot ) // scroll up one line
							vt100_Escape((unsigned char *)"D", 1);
						else {
							int x = cursor_x-line[cursor_y];
							cursor_x = line[++cursor_y]+x;
						}
					}
					else {	//not on alter screen, store LF at end of line
						cursor_x = line[cursor_y+1];
//if the next line is not empty, assum it's a ESC[2J cleared buffer like in top
						if ( line[cursor_y+2]!=0 ) cursor_x--;
						attr[cursor_x] = c_attr;
						buff[cursor_x++] = 0x0a;
						next_line();		//hard line feed
					}
					break;
		case 0x0d: 	if ( cursor_x-line[cursor_y]==size_x+1 && *p!=0x0a ) 
						next_line();		//soft line feed
					else
						cursor_x = line[cursor_y]; 
					break;
		case 0x1b: 	p = vt100_Escape( p, zz-p );
					break;
		case 0xff: 	p = telnet_options(p);
					break;
		case 0xe2:  if ( bAlterScreen ) {//utf8 box drawing hack
						c = ' ';
						if ( *p++==0x94 ) {
							switch ( *p ) {
							case 0x80:
							case 0xac:
							case 0xb4:
							case 0xbc: c='_'; break;
							case 0x82:
							case 0x94:
							case 0x98:
							case 0x9c:
							case 0xa4: c='|'; break;
							}
						}
						p++;
					}//fall through
		default:	if ( bGraphic ) switch ( c ) {//charset 2 box drawing
						case 'q': c='_'; break;
						case 'x': c='|';
						case 't':
						case 'u':
						case 'm':
						case 'j': c='|'; break;
						case 'l':
						case 'k': c=' '; break;
						default: c = '?';
					}
					if ( bInsert ) 			//insert one space
						vt100_Escape((unsigned char *)"[1@",3);	
					if ( cursor_x-line[cursor_y]>=size_x ) {
						int char_cnt = 0;
						for ( int i=line[cursor_y]; i<cursor_x; i++ )
							if ( (buff[i]&0xc0)!=0x80 ) char_cnt++;
						if ( char_cnt==size_x ) {
							if ( bAlterScreen ) 
								cursor_x--;	//don't overflow in vi
							else
								next_line();
						}
					} 
					attr[cursor_x] = c_attr;
					buff[cursor_x++] = c;
					if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
		}
	}
	if ( !bPrompt )
		if (strncmp(sPrompt, buff+cursor_x-iPrompt, iPrompt)==0) 
			bPrompt = true;
	if ( visible() && redraw_complete ) {
		redraw_complete = false;
		Fl::awake( this );
	}
}

const unsigned char *Fl_Term::vt100_Escape( const unsigned char *sz, int cnt )
{
	const unsigned char *zz = sz+cnt;
	bEscape = true;
	while ( sz<zz && bEscape ){
		ESC_code[ESC_idx++] = *sz++;
		switch( ESC_code[0] ) {
		case '[':
			if ( isalpha(ESC_code[ESC_idx-1])
				|| ESC_code[ESC_idx-1]=='@'
				|| ESC_code[ESC_idx-1]=='`' ) {
				bEscape = false;
				int n0=1, n1=1, n2=1;
				if ( ESC_idx>1 ) {
					char *p = strchr(ESC_code,';');
					if ( p != NULL ) {
						char *p1 = strchr(p+1, ';');
						if ( p1!=NULL ) {
							n0 = atoi(ESC_code+1);
							n1 = atoi(p+1);
							n2 = atoi(p1+1);
						}
						else {
							n0 = 0;
							n1 = atoi(ESC_code+1);
							n2 = atoi(p+1);
						}
					}
					else
						if ( isdigit(ESC_code[1]) ) {
							n0 = atoi(ESC_code+1);
							n1 = 0; 
							n2 = 0;
						}
				}
				int x;
				switch ( ESC_code[ESC_idx-1] ) {
				case 'A': //cursor up n0 times
					x = cursor_x-line[cursor_y];
					cursor_y -=n0;
					cursor_x = line[cursor_y]+x;
					break;
				case 'd'://line position absolute
					if ( n0>size_y ) n0 = size_y;
					x = cursor_x-line[cursor_y];
					cursor_y = screen_y+n0-1;
					cursor_x = line[cursor_y]+x;
					break;
				case 'e': //line position relative
				case 'B': //cursor down n0 times
					x = cursor_x-line[cursor_y];
					cursor_y += n0;
					cursor_x = line[cursor_y]+x;
					break;
				case 'I': //cursor forward n0 tab stops
					n0 *= 8;	
					//fall through
				case 'a': //character position relative
				case 'C': //cursor forward n0 times
					while ( n0-->0 ) {
						if ( (buff[++cursor_x]&0xc0)==0x80 )
							while ( (buff[++cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'Z': //cursor backward n0 tab stops
					n0 *= 8;
					//fall through
				case 'D': //cursor backward n0 times
					while ( n0-->0 ) {
						if ( (buff[--cursor_x]&0xc0)==0x80 )
							while ( (buff[--cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'E': //cursor to begining of next line n0 times
					cursor_y += n0;
					cursor_x = line[cursor_y];
					break;
				case 'F': //cursor to begining of previous line n0 times
					cursor_y -= n0;
					cursor_x = line[cursor_y];
					break;
				case '`': //character position absolute
				case 'G': //cursor to n0th position from left
					n1 = cursor_y-screen_y+1;
					n2 = n0;				//fall through to 'H'
				case 'f': //horizontal and vertical position forced
					if ( n1==0 ) n1=1;
					if ( n2==0 ) n2=1;		//fall through to 'H'
				case 'H': //cursor to line n1, postion n2
					if ( n1>size_y ) n1 = size_y;
					if ( n2>size_x ) n2 = size_x;
					cursor_y = screen_y+n1-1;
					cursor_x = line[cursor_y];
					while ( --n2>0 ) {
						cursor_x++;
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x++;
					}
					break;
				case 'J': //[J kill till end, 1J begining, 2J entire screen
					if ( ESC_code[ESC_idx-2]=='[' ) {
					//[J, tinycore use this for CLI editing
						if ( !bAlterScreen ) {
							int i=cursor_y+1; line[i]=cursor_x; 
							while (++i<=screen_y+size_y) line[i]=0;
							break;
						}
						else //tinycore use this to clear alterScreen 
							screen_y = cursor_y;
					}
					/*[2J, mostly used after [?1049h to clear screen
					  and when screen size changed during vi or raspi-config
					  flashwave TL1 use it without [?1049h for splash screen 
					  freeBSD use it without [?1049h* for top and vi 		*/
					cursor_y = screen_y; cursor_x = line[cursor_y];
					for ( int i=0; i<size_y; i++ ) { 
						memset(buff+cursor_x, ' ', size_x);
						memset(attr+cursor_x,   0, size_x);
						cursor_x += size_x;
						next_line();
					}
					cursor_y = --screen_y; cursor_x = line[cursor_y];
					break;
				case 'K': {//[K erase till line end, 1K begining, 2K entire line
					if ( ESC_code[ESC_idx-2]=='[' ) n0 = 0;
					int a=0, z=0;
					if ( line[cursor_y+1]==0 ) line[cursor_y+1] = cursor_x;
					switch ( n0 ) {
						case 0:	a=cursor_x; z=line[cursor_y+1]; break;
						case 1: z=cursor_x; a=line[cursor_y]; break;
						case 2: a=line[cursor_y]; z=line[cursor_y+1]; break;
					}
					memset(buff+a, ' ', z-a);
					memset(attr+a,   0, z-a);
					}
					break;
				case 'L': //insert n0 lines
					for ( int i=roll_bot; i>cursor_y-screen_y; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x );
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x );
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[cursor_y+i], ' ', size_x);
						memset(attr+line[cursor_y+i],   0, size_x);
					}
					break;
				case 'M': //delete n0 lines
					for ( int i=cursor_y-screen_y; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x);
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[screen_y+roll_bot-i], ' ', size_x);
						memset(attr+line[screen_y+roll_bot-i],   0, size_x);
					}
					break;
				case '@': //insert n0 spaces
					for ( int i=line[cursor_y+1]; i>=cursor_x; i-- ){
						buff[i+n0]=buff[i]; 
						attr[i+n0]=attr[i];
					}
					if ( !bAlterScreen ) {
						line[cursor_y+1]+=n0;
						if ( line[cursor_y+2]!=0 ) line[cursor_y+2]+=n0;
						memset(buff+cursor_x, ' ', n0);
						memset(attr+cursor_x,   0, n0);
					}
					break;
				case 'P': //delete n0 characters
					for ( int i=cursor_x; i<line[cursor_y+1]-n0; i++ ) {
						buff[i]=buff[i+n0];
						attr[i]=attr[i+n0];
					}
					if ( !bAlterScreen ) {
					//when editing command input longer than a single line
						line[cursor_y+1]-=n0;
						if ( line[cursor_y+2]>0 ) line[cursor_y+2]-=n0;
						memset(buff+line[cursor_y+1], ' ', n0);
						memset(attr+line[cursor_y+1],   0, n0);
					}
					break;
				case 'X': //erase n0 characters
					for ( int i=0; i<n0; i++) {
						buff[cursor_x+i]=' ';
						attr[cursor_x+i]=0;
					}
					break;
				case 'S': // scroll up n0 lines
					for ( int i=roll_top; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x);
					}
					memset(buff+line[screen_y+roll_bot-n0+1], ' ', n0*size_x);
					memset(attr+line[screen_y+roll_bot-n0+1],   0, n0*size_x);
					break;
				case 'T': // scroll down n0 lines
					for ( int i=roll_bot; i>=roll_top+n0; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x);
					}
					memset(buff+line[screen_y+roll_top], ' ', n0*size_x);
					memset(attr+line[screen_y+roll_top],   0, n0*size_x);
					break;
				case 'h':
					if ( ESC_code[1]=='4' ) bInsert=true;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 ) bAppCursor=true;
						if ( n0==25 ) bCursor=true;
						if ( n0==1049 ) {	//?1049h enter alternate screen
							bAlterScreen = true;
							screen_y = cursor_y; cursor_x = line[cursor_y];
/* 	should clear alterScreen here, but all apps(vi, raspi-config etc) use 
	ESC[2J or ESC[J to clear the screen following ESC[?1049h any way 	*/
						}
					}
					break;
				case 'l':
					if ( ESC_code[1]=='4' ) bInsert=false;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 )  bAppCursor=false;
						if ( n0==25 ) bCursor=false;
						if ( n0==1049 ) { 	//?1049l exit alternate screen
							bAlterScreen = false;
							cursor_y = screen_y; cursor_x = line[cursor_y];
							for ( int i=1; i<=size_y+1; i++ ) 
								line[cursor_y+i] = 0;
							screen_y = cursor_y-size_y+1;
							if ( screen_y<0 ) screen_y = 0;
						}
					}
					break;
				case 'm': //text style, color attributes
					if ( n0==0 && n2!=1 ) n0 = n2;	//ESC[0;0m	ESC[01;34m
					switch ( int(n0/10) ) {			//ESC[34m
					case 0: if ( n0%10==7 ) {c_attr = 0x70; break;}
					case 1:
					case 2: c_attr = 7; break;
					case 3: if ( n0==39 ) n0 = 37;	//39 default foreground
							c_attr = (c_attr&0xf0)+n0%10; break;
					case 4: if ( n0==49 ) n0 = 40;	//49 default background
							c_attr = (c_attr&0x0f)+((n0%10)<<4); break;
					}
					break;
				case 'r': roll_top=n1-1; roll_bot=n2-1; break;
				case 's': //save cursor
					save_x = cursor_x-line[cursor_y];
					save_y = cursor_y-screen_y;
					break;
				case 'u': //restore cursor
					cursor_y = save_y+screen_y;
					cursor_x = line[cursor_y]+save_x;
					break;
				}
			}
			break;
		case 'D': // scroll up one line
			for ( int i=roll_top; i<roll_bot; i++ ) {
				memcpy(buff+line[screen_y+i],buff+line[screen_y+i+1],size_x);
				memcpy(attr+line[screen_y+i],attr+line[screen_y+i+1],size_x);
			}
			memset(buff+line[screen_y+roll_bot], ' ', size_x);
			memset(attr+line[screen_y+roll_bot],   0, size_x);
			bEscape = false;
			break;
		case 'F': //cursor to lower left corner
			cursor_y = screen_y+size_y-1;
			cursor_x = line[cursor_y];
			bEscape = false;
			break;
		case 'M': // scroll down one line
			for ( int i=roll_bot; i>roll_top; i-- ) {
				memcpy(buff+line[screen_y+i],buff+line[screen_y+i-1],size_x);
				memcpy(attr+line[screen_y+i],attr+line[screen_y+i-1],size_x);
			}
			memset(buff+line[screen_y+roll_top], ' ', size_x);
			memset(attr+line[screen_y+roll_top],   0, size_x);
			bEscape = false;
			break;
		case ']': //set window title
			if ( ESC_code[ESC_idx-1]==';' ) {
				if ( ESC_code[1]=='0' ) {
					bTitle = true;
					title_idx = 0;
				}
				bEscape = false;
			}
			break;
		case '(': //character sets, 0 for line drawing
			if ( ESC_idx==2 ) {
				bGraphic = (ESC_code[1]=='0');
				bEscape = false;
			}
			break;
		default: bEscape = false;
		}
		if ( ESC_idx==20 ) bEscape = false;
		if ( !bEscape ) { ESC_idx=0; memset(ESC_code, 0, 20); }
	}
	return sz;
}
void Fl_Term::logg( const char *fn )
{
	if ( fpLogFile!=NULL ) {
		fclose( fpLogFile );
		fpLogFile = NULL;
		disp("\r\n\033[32m***Log file closed***\033[37m\r\n");
	}
	else {
		fpLogFile = fl_fopen( fn, "wb" );
		if ( fpLogFile != NULL ) {
			disp("\r\n\033[32m***");
			disp(fn);
			disp(" opened for logging***\033[37m\r\n");
		}
	}
}
void Fl_Term::srch( const char *sstr )
{
	int l = strlen(sstr);
	char *p = buff+sel_left;
	if ( sel_left==sel_right ) p = buff+cursor_x;
	while ( --p>=buff+l ) {
		int i;
		for ( i=l-1; i>=0; i-- ) 
			if ( sstr[i]!=p[i-l] ) break;
		if ( i==-1 ) {
			sel_left = p-l-buff;
			sel_right = sel_left+l;
			while ( line[screen_y+scroll_y]>sel_left ) scroll_y--;
			break;
		}
	}
	if ( p<buff+l ) sel_left = sel_right = scroll_y = 0;
	redraw();
}
void Fl_Term::learn_prompt()
{//capture prompt for scripting
	sPrompt[0] = buff[cursor_x-2];
	sPrompt[1] = buff[cursor_x-1];
	sPrompt[2] = 0;
	iPrompt = 2;
}
char *Fl_Term::mark_prompt()
{
	bPrompt = false;
	recv0 = cursor_x;
	return buff+cursor_x;
}
int Fl_Term::waitfor_prompt( )
{
	int oldlen = recv0;
	for ( int i=0; i<iTimeOut*10 && !bPrompt; i++ ) {
		sleep_ms(100);
		if ( cursor_x>oldlen ) { i=0; oldlen=cursor_x; }
	}
	bPrompt = true;
	return cursor_x - recv0;
}
void Fl_Term::disp(const char *buf)
{
	recv0 = cursor_x;
	append(buf, strlen(buf));
}
void Fl_Term::send(const char *buf)
{
	write(buf, strlen(buf));
}
int Fl_Term::recv(char **preply)
{
	if ( preply!=NULL ) *preply = buff+recv0;
	int len = cursor_x-recv0;
	recv0 = cursor_x;
	return len;
}
void Fl_Term::connect(const char *hostname)
{
	if ( live() ) {
		if ( strncmp(hostname, "disconn", 7)==0 ) host->disconn();
		return;
	}
	
	if ( host!=NULL ) {
		delete host;
		host = NULL;
	}
	if ( strncmp(hostname, "ssh " , 4)==0 ) {
		host = new sshHost(hostname+4);
	}
	else if ( strncmp(hostname, "sftp ", 5)==0 ) {
		host = new sftpHost(hostname+5);
	}
	else if ( strncmp(hostname, "telnet ", 7)==0 ) {
		host = new tcpHost(hostname+7);
	}
	else if ( strncmp(hostname, "serial ", 7)==0 ) {
		host = new comHost(hostname+7);
	}
	else if ( strncmp(hostname, "netconf ", 8)==0 ) {
		char netconf[256];
		strcpy(netconf, "-s ");
		strcat(netconf, hostname);
		host = new sshHost(netconf);
	}
#ifdef WIN32
	else if ( strncmp(hostname, "ftpd ", 5)==0 ) {
		host = new ftpdHost(hostname+5);
	}
	else if ( strncmp(hostname, "tftpd ", 6)==0 ) {
		host = new tftpdHost(hostname+6);
	}
#endif
	if ( host!=NULL ) {
		char label[32];
		strncpy(label, host->name(), 28);
		label[28]=0;
		strcat(label, "  x");
		copy_label(label);
		host->callback(host_cb, this);
		host->connect();
	}
}
int Fl_Term::cmd(const char *cmd, char **preply)
{
	int rc = 0;
	if ( *cmd!='!' ) {
		if ( live() ) {
			char *p = mark_prompt();
			send(cmd);
			send("\r");
			rc = waitfor_prompt();
			if ( preply!=NULL ) *preply = p;
		}
		else {
			disp(cmd); 
			disp("\n");
		}
	}
	else {
		cmd++;
		if ( strncmp(cmd,"Clear",5)==0 ) clear();
		else if ( strncmp(cmd,"Log ",4)==0 ) logg( cmd+4 );
		else if ( strncmp(cmd,"Find ",5)==0 ) srch(cmd+5);
		else if ( strncmp(cmd,"Wait ",5)==0 ) sleep_ms(atoi(cmd+5)*1000);
		else if ( strncmp(cmd,"Disp ",5)==0 ) disp(cmd+5);
		else if ( strncmp(cmd,"Recv", 4)==0 ) rc = recv(preply);
		else if ( strncmp(cmd,"Send ",5)==0 ) {
			mark_prompt();
			send(cmd+5);
		}
		else if ( strncmp(cmd,"Hostname",8)==0 ) {
			if ( preply!=NULL ) {
				*preply = (char *)label();
				rc = strlen(*preply);
			}
		}
		else if ( strncmp(cmd,"Selection",9)==0) {
			if ( preply!=NULL ) *preply = buff+sel_left;
			rc = sel_right-sel_left;
		}
		else if ( strncmp(cmd,"scp ",4)==0 ) rc = scp(strdup(cmd+4),preply);
		else if ( strncmp(cmd,"tun",3)==0 )  rc = tun(strdup(cmd+3),preply);
		else if ( strncmp(cmd,"Prompt ",7)==0 ) {
			strncpy(sPrompt, cmd+7, 31);
			sPrompt[31] = 0;
			fl_decode_uri(sPrompt);
			iPrompt = strlen(sPrompt);
		}
		else if ( strncmp(cmd,"Waitfor ",8)==0 ) { 
			char *p = buff+recv0;
			bWait = true;
			for ( int i=0; i<iTimeOut*10&&bWait; i++ ) {
				buff[cursor_x]=0;
				if ( strstr(p, cmd+8)!=NULL ) {
					if ( preply!=NULL ) *preply = p;
					rc = cursor_x-recv0;
					break;
				}
				sleep_ms(100);
			}
		}
		else if ( strncmp(cmd,"Timeout",7)==0 ) iTimeOut = atoi(cmd+8);
		else 
			connect(cmd);
	}
	return rc;
}
void Fl_Term::putxml(const char *msg, int len)
{
	const char *p=msg, *q;
	const char spaces[256]="\r\n                                               \
                                                                              ";
	if ( strncmp(msg, "<?xml ", 6)==0 ) {
		xmlIndent = 0;
		xmlTagIsOpen = true;
	}
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>msg ) append(msg, p-msg);
	while ( *p!=0 && p<msg+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ' ) p++;
		if ( *p=='<' ) { //tag
			if ( p[1]=='/' ) {
				if ( !xmlTagIsOpen ) {
					xmlIndent -= 2;
					append(spaces, xmlIndent);
				}
				xmlTagIsOpen = false;
			}
			else {
				if ( xmlTagIsOpen ) xmlIndent+=2;
				append(spaces, xmlIndent);
				xmlTagIsOpen = true;
			}
			append("\033[32m",5);
			q = strchr(p, '>');
			if ( q==NULL ) q = p+strlen(p);
			const char *r = strchr(p, ' ');
			if ( r!=NULL && r<q ) {
				append(p, r-p);
				append("\033[34m",5);
				append(r, q-r);
			}
			else
				append(p, q-p);
			append("\033[32m>",6);
			p = q;
			if ( *q=='>' ) {
				p++;
				if ( q[-1]=='/' ) xmlTagIsOpen = false;
			}
		}
		else {			//data
			append("\033[33m",5);
			q = strchr(p, '<');
			if ( q==NULL ) q = p+strlen(p);
			append(p, q-p);
			p = q;
		}
	}
}

void Fl_Term::scripter(char *cmds)
{
	bScriptRunning = true;
	bScriptPaused = false;
	char *p=cmds, *p1;	
	do {
		p1 = strchr(p, 0x0a);
		if ( p1!=NULL ) *p1++ = 0;
		cmd(p, NULL);
		while ( bScriptPaused ) sleep_ms(100);
	}
	while ( (p=p1)!=NULL && bScriptRunning );
	bScriptPaused = false;
	bScriptRunning = false;
	free(cmds);
}
void Fl_Term::pause_script()
{
	bScriptPaused = true;
}
void Fl_Term::stop_script()
{
	bScriptPaused = bScriptRunning = false;
}

void Fl_Term::keepalive(int interval)
{
	host->keepalive(interval);
}
char *Fl_Term::ssh_gets(const char *prompt, int echo)
{
	return ((sshHost *)host)->ssh_gets(prompt, echo);
}
void Fl_Term::term_pwd(char *dst)
{
	char *p1, *p2;
	cmd("pwd", &p2);
	p1 = strchr(p2, 0x0a);
	if ( p1!=NULL ) {
		p2 = p1+1;
		p1 = strchr(p2, 0x0a);
		if ( p1!=NULL ) {
			strncpy(dst, p2, p1-p2);
			dst[p1-p2]=0;
		}
	}
}
int Fl_Term::scp(char *cmd, char **preply)
{
	if ( host==NULL ) {
		disp("not connected yet\n");
		return 0;
	}
	if ( host->type()!=HOST_SSH ) {
		disp("not ssh connection\n");
		return 0;
	}

	learn_prompt();
	char *reply = mark_prompt();
	char *p = strchr(cmd, ' ');
	if ( p!=NULL ) {
		*p++ = 0;
		char *local, *remote, *rpath, rlist[1024];
		if ( *cmd==':' ) {			//scp_read
			local = p; remote = cmd+1;
			strcpy(rlist, "ls -1  ");
		}
		else {						//scp_write
			local = cmd; remote = p+1;
			strcpy(rlist, "ls -ld ");
		}
		if ( *remote=='/') 			//get remote dir
			strcpy(rlist+7, remote);
		else {
			term_pwd(rlist+7);
			if ( *remote ) {
				strcat(rlist, "/");
				strcat(rlist, remote);
			}
		}
		if ( Fl_Term::cmd(rlist, &rpath)>0 ) {
			reply = mark_prompt();
			char *p = strchr(rpath, 0x0a);
			if ( p!=NULL ) {
				if ( *cmd==':' ) {	//scp_read
					remote = strdup(p+1);
					((sshHost *)host)->scp_read(local, remote);
				}
				else {				//scp_write
					if ( p[1]=='d' ) strcat(rlist, "/");
					remote = strdup(rlist+7);
					((sshHost *)host)->scp_write(local, remote);
				}
				free(remote);
			}
		}	
	}
	free(cmd);
	host->write("\r", 1);
	if ( preply!=NULL ) *preply = reply;
	return waitfor_prompt();
}
int Fl_Term::tun(char *cmd, char **preply)
{
	if ( host==NULL ) {
		disp("not connected yet\n");
		return 0;
	}
	if ( host->type()!=HOST_SSH ) {
		disp("not ssh connection\n");
		return 0;
	}

	if ( preply!=NULL ) *preply = mark_prompt();
	((sshHost *)host)->tun(cmd);
	free(cmd);
	return waitfor_prompt();
}
void Fl_Term::copier(char *files)
{
	if ( host==NULL ) return;
	if ( host->type()!=HOST_SSH && host->type()!=HOST_SFTP ) return;
	
	char rdir[1024];
	term_pwd(rdir);
	strcat(rdir, "/");

	char *p=files, *p1;
	do {
		p1 = strchr(p, 0x0a);
		if ( p1!=NULL ) *p1++ = 0;
		if ( host->type()==HOST_SSH )
			((sshHost *)host)->scp_write(p, rdir);
		else if ( host->type()==HOST_SFTP )
			((sftpHost *)host)->sftp_put(p, rdir);
	}
	while ( (p=p1)!=NULL ); 
	host->write("\r",1);
	free(files);
}
void Fl_Term::run_script(char *script)
{
	if ( !live() ) {
		puts(script, strlen(script));
		free(script);
		return;
	}
	if ( host->type()==HOST_CONF ) {
		if ( bEcho ) putxml(script, strlen(script));
		host->write(script, strlen(script));
		free(script);
		return;
	}

	learn_prompt();
	char *p0 = script;
	char *p1=strchr(p0, 0x0a);
	if ( p1!=NULL ) *p1=0;
	struct stat sb;				//is this a list of files?
	int rc = stat(p0, &sb);
	if ( p1!=NULL ) *p1=0x0a;

	if ( rc!=-1 ) {
		if ( host->type()==HOST_SSH || host->type()==HOST_SFTP ) {
			std::thread scripterThread(&Fl_Term::copier, this, script);
			scripterThread.detach();
		}
	}
	else {
		if ( !bScriptRunning ) {
			std::thread scripterThread(&Fl_Term::scripter, this, script);
			scripterThread.detach();
		}
		else 
			disp("\033[31mScript is already running\033[37m\n");
	}
}

#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
unsigned char NEGOBEG[]={0xff, 0xfb, 0x03, 0xff, 0xfd, 0x03, 0xff, 0xfd, 0x01};
unsigned char TERMTYPE[]={0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0};
const unsigned char *Fl_Term::telnet_options( const unsigned char *p )
{
	unsigned char negoreq[]={0xff,0,0,0, 0xff, 0xf0};
	switch ( *p++ ) {
		case TNO_DO:
			if ( *p==TNO_TERMTYPE || *p==TNO_NEWENV || *p==TNO_ECHO ) {
				negoreq[1]=TNO_WILL; negoreq[2]=*p;
				write((const char *)negoreq, 3);
				if ( *p==TNO_ECHO ) bEcho = true;
			}
			else  {						// if ( *p!=TNO_AHEAD ), 08/10 why?
				negoreq[1]=TNO_WONT; negoreq[2]=*p;
				write((const char *)negoreq, 3);
			}
			break;
		case TNO_SUB:
			if ( *p==TNO_TERMTYPE ) {
				write((const char *)TERMTYPE, sizeof(TERMTYPE));
			}
			if ( *p==TNO_NEWENV ) {
				negoreq[1]=TNO_SUB; negoreq[2]=*p;
				write((const char *)negoreq, 6);
			}
			p += 3;
			break;
		case TNO_WILL: 
			if ( *p==TNO_ECHO ) bEcho = false;
			negoreq[1]=TNO_DO; negoreq[2]=*p;
			write((const char *)negoreq, 3);
			break;
		case TNO_WONT: 
			negoreq[1]=TNO_DONT; negoreq[2]=*p;
			write((const char *)negoreq, 3);
		   break;
		case TNO_DONT:
			break;
	}
	return p+1; 
}