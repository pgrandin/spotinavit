#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>

DBusConnection* conn;
DBusMessage* msg;

void dbus_init() 
{
   DBusMessage* reply;
   DBusMessageIter args;
   DBusError err;
   int ret;
   char* param;

   printf("Listening for method calls\n");

   // initialise the error
   dbus_error_init(&err);
   
   // connect to the bus and check for errors
   conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
   if (dbus_error_is_set(&err)) { 
      fprintf(stderr, "Connection Error (%s)\n", err.message); 
      dbus_error_free(&err); 
   }
   if (NULL == conn) {
      fprintf(stderr, "Connection Null\n"); 
      exit(1); 
   }
   
   // request our name on the bus and check for errors
   ret = dbus_bus_request_name(conn, "org.kazer.navifyplugin", DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
   if (dbus_error_is_set(&err)) { 
      fprintf(stderr, "Name Error (%s)\n", err.message); 
      dbus_error_free(&err);
   }
   if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) { 
      fprintf(stderr, "Not Primary Owner (%d)\n", ret);
      exit(1); 
   }
}

void dbus_process()
{
      // non blocking read of the next available message
      dbus_connection_read_write(conn, 0);
      msg = dbus_connection_pop_message(conn);

      // loop again if we haven't got a message
      if (NULL == msg) { 
         return;
      }
      
      // check this is a method call for the right interface & method
      if (dbus_message_is_method_call(msg, "test.method.Type", "Method")) 
      	 printf("call received\n");
         //reply_to_method_call(msg, conn);

      // free the message
      dbus_message_unref(msg);
}

void dbus_shutdown()
{
   // close the connection
   dbus_connection_close(conn);
}
