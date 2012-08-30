/***************************************************************************
 *            chat.c
 *
 *  Sun Jun  5 19:34:18 2005
 *  Copyright  2005  Simon Morlat
 *  Email simon dot morlat at linphone dot org
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
 #include "linphonecore.h"
 #include "private.h"
 
 LinphoneChatRoom * linphone_core_create_chat_room(LinphoneCore *lc, const char *to){
	LinphoneAddress *parsed_url=NULL;

	if ((parsed_url=linphone_core_interpret_url(lc,to))!=NULL){
		LinphoneChatRoom *cr=ms_new0(LinphoneChatRoom,1);
		cr->lc=lc;
		cr->peer=linphone_address_as_string(parsed_url);
		cr->peer_url=parsed_url;
		lc->chatrooms=ms_list_append(lc->chatrooms,(void *)cr);
		return cr;
	}
	return NULL;
 }
 
 
 void linphone_chat_room_destroy(LinphoneChatRoom *cr){
	LinphoneCore *lc=cr->lc;
	lc->chatrooms=ms_list_remove(lc->chatrooms,(void *) cr);
	linphone_address_destroy(cr->peer_url);
	ms_free(cr->peer);
	if (cr->op)
		 sal_op_release(cr->op);
 }


static void _linphone_chat_room_send_message(LinphoneChatRoom *cr, LinphoneChatMessage* msg){
	const char *route=NULL;
	const char *identity=linphone_core_find_best_identity(cr->lc,cr->peer_url,&route);
	SalOp *op=NULL;
	LinphoneCall *call;
	if((call = linphone_core_get_call_by_remote_address(cr->lc,cr->peer))!=NULL){
		if (call->state==LinphoneCallConnected ||
		    call->state==LinphoneCallStreamsRunning ||
		    call->state==LinphoneCallPaused ||
		    call->state==LinphoneCallPausing ||
		    call->state==LinphoneCallPausedByRemote){
			ms_message("send SIP message through the existing call.");
			op = call->op;
			call->pending_message=msg;
		}
	}
	if (op==NULL){
		/*sending out of calls*/
		op = sal_op_new(cr->lc->sal);
		sal_op_set_route(op,route);
		if (cr->op!=NULL){
			sal_op_release (cr->op);
			cr->op=NULL;
		}
		cr->op=op;
		sal_op_set_user_pointer(op, msg); /*if out of call, directly store msg*/
	}
	sal_text_send(op,identity,cr->peer,msg->message);
}

void linphone_chat_room_send_message(LinphoneChatRoom *cr, const char *msg) {
	_linphone_chat_room_send_message(cr,linphone_chat_room_create_message(cr,msg));
}
bool_t linphone_chat_room_matches(LinphoneChatRoom *cr, const LinphoneAddress *from){
	if (linphone_address_get_username(cr->peer_url) && linphone_address_get_username(from) && 
		strcmp(linphone_address_get_username(cr->peer_url),linphone_address_get_username(from))==0) return TRUE;
	return FALSE;
}

void linphone_chat_room_text_received(LinphoneChatRoom *cr, LinphoneCore *lc, const LinphoneAddress *from, const char *msg){
	if (lc->vtable.text_received!=NULL) lc->vtable.text_received(lc, cr, from, msg);
}

void linphone_core_text_received(LinphoneCore *lc, const char *from, const char *msg){
	MSList *elem;
	LinphoneChatRoom *cr=NULL;
	LinphoneAddress *addr;
	char *cleanfrom;

	addr=linphone_address_new(from);
	linphone_address_clean(addr);
	for(elem=lc->chatrooms;elem!=NULL;elem=ms_list_next(elem)){
		cr=(LinphoneChatRoom*)elem->data;
		if (linphone_chat_room_matches(cr,addr)){
			break;
		}
		cr=NULL;
	}
	cleanfrom=linphone_address_as_string(addr);
	if (cr==NULL){
		/* create a new chat room */
		cr=linphone_core_create_chat_room(lc,cleanfrom);
	}

	linphone_address_destroy(addr);
	linphone_chat_room_text_received(cr,lc,cr->peer_url,msg);
	ms_free(cleanfrom);
}


void linphone_chat_room_set_user_data(LinphoneChatRoom *cr, void * ud){
	cr->user_data=ud;
}
void * linphone_chat_room_get_user_data(LinphoneChatRoom *cr){
	return cr->user_data;
}
const LinphoneAddress* linphone_chat_room_get_peer_address(LinphoneChatRoom *cr) {
	return cr->peer_url;
}

LinphoneChatMessage* linphone_chat_room_create_message(const LinphoneChatRoom *cr,const char* message) {
	LinphoneChatMessage* msg = ms_new0(LinphoneChatMessage,1);
	msg->chat_room=(LinphoneChatRoom*)cr;
	msg->message=ms_strdup(message);
	return msg;
}
void linphone_chat_message_destroy(LinphoneChatMessage* msg) {
	if (msg->message) ms_free((void*)msg->message);
	ms_free((void*)msg);
}
void linphone_chat_room_send_message2(LinphoneChatRoom *cr, LinphoneChatMessage* msg,LinphoneChatMessageStateChangeCb status_cb,void* ud) {
	msg->cb=status_cb;
	msg->cb_ud=ud;
	_linphone_chat_room_send_message(cr, msg);
}

const char* linphone_chat_message_state_to_string(const LinphoneChatMessageState state) {
	switch (state) {
		case LinphoneChatMessageStateIdle:return "LinphoneChatMessageStateIdle"; 
		case LinphoneChatMessageStateInProgress:return "LinphoneChatMessageStateInProgress";
		case LinphoneChatMessageStateDelivered:return "LinphoneChatMessageStateDelivered";
		case  LinphoneChatMessageStateNotDelivered:return "LinphoneChatMessageStateNotDelivered";
		default: return "Unknown state";
	}
	
}
/**
 * user pointer set function
 */
void linphone_chat_message_set_user_data(LinphoneChatMessage* message,void* ud) {
	message->message_userdata=ud;
}
/**
 * user pointer get function
 */
void* linphone_chat_message_get_user_data(const LinphoneChatMessage* message) {
	return message->message_userdata;
}