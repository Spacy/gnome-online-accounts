/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012, 2013 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "goaewsclient.h"
#include "goalogging.h"
#include "goaprovider.h"
#include "goaexchangeprovider.h"
#include "goaeditablelabel.h"
#include "goaspinnerbutton.h"
#include "goautils.h"

/**
 * GoaExchangeProvider:
 *
 * The #GoaExchangeProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaExchangeProvider
{
  /*< private >*/
  GoaProvider parent_instance;
};

typedef struct _GoaExchangeProviderClass GoaExchangeProviderClass;

struct _GoaExchangeProviderClass
{
  GoaProviderClass parent_class;
};

/**
 * SECTION:goaexchangeprovider
 * @title: GoaExchangeProvider
 * @short_description: A provider for Microsoft Exchange servers
 *
 * #GoaExchangeProvider is used to access Microsoft Exchange servers.
 */

G_DEFINE_TYPE_WITH_CODE (GoaExchangeProvider, goa_exchange_provider, GOA_TYPE_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "exchange",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return "exchange";
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Microsoft Exchange"));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaCalendar *calendar;
  GoaContacts *contacts;
  GoaExchange *exchange;
  GoaMail *mail;
  GoaPasswordBased *password_based;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean mail_enabled;
  gboolean ret;

  account = NULL;
  calendar = NULL;
  contacts = NULL;
  exchange = NULL;
  mail = NULL;
  password_based = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_exchange_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

  password_based = goa_object_get_password_based (GOA_OBJECT (object));
  if (password_based == NULL)
    {
      password_based = goa_password_based_skeleton_new ();
      /* Ensure D-Bus method invocations run in their own thread */
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (password_based),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      goa_object_skeleton_set_password_based (object, password_based);
      g_signal_connect (password_based,
                        "handle-get-password",
                        G_CALLBACK (on_handle_get_password),
                        NULL);
    }

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          const gchar *email_address;

          email_address = goa_account_get_presentation_identity (account);
          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail), "email-address", email_address, NULL);
          goa_object_skeleton_set_mail (object, mail);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  /* Calendar */
  calendar = goa_object_get_calendar (GOA_OBJECT (object));
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  if (calendar_enabled)
    {
      if (calendar == NULL)
        {
          calendar = goa_calendar_skeleton_new ();
          goa_object_skeleton_set_calendar (object, calendar);
        }
    }
  else
    {
      if (calendar != NULL)
        goa_object_skeleton_set_calendar (object, NULL);
    }

  /* Contacts */
  contacts = goa_object_get_contacts (GOA_OBJECT (object));
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  if (contacts_enabled)
    {
      if (contacts == NULL)
        {
          contacts = goa_contacts_skeleton_new ();
          goa_object_skeleton_set_contacts (object, contacts);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }

  /* Exchange */
  exchange = goa_object_get_exchange (GOA_OBJECT (object));
  if (exchange == NULL)
    {
      gchar *host;

      host = g_key_file_get_string (key_file, group, "Host", NULL);
      exchange = goa_exchange_skeleton_new ();
      g_object_set (G_OBJECT (exchange),
                    "host", host,
                    NULL);
      goa_object_skeleton_set_exchange (object, exchange);
      g_free (host);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);

      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "MailEnabled");
      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ContactsEnabled");
    }

  ret = TRUE;

 out:
  if (exchange != NULL)
    g_object_unref (exchange);
  if (contacts != NULL)
    g_object_unref (contacts);
  if (calendar != NULL)
    g_object_unref (calendar);
  if (mail != NULL)
    g_object_unref (mail);
  if (password_based != NULL)
    g_object_unref (password_based);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  GVariant *credentials;
  GoaAccount *account;
  GoaEwsClient *ews_client;
  GoaExchange *exchange;
  gboolean accept_ssl_errors;
  gboolean ret;
  const gchar *email_address;
  const gchar *server;
  const gchar *username;
  gchar *password;

  credentials = NULL;
  ews_client = NULL;
  password = NULL;

  ret = FALSE;

  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    {
      if (error != NULL)
        {
          g_prefix_error (error, _("Credentials not found in keyring (%s, %d): "),
                          g_quark_to_string ((*error)->domain), (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  account = goa_object_peek_account (object);
  email_address = goa_account_get_presentation_identity (account);
  username = goa_account_get_identity (account);

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      if (error != NULL)
        {
          *error = g_error_new (GOA_ERROR,
                                GOA_ERROR_NOT_AUTHORIZED,
                                _("Did not find password with username `%s' in credentials"),
                                username);
        }
      goto out;
    }

  exchange = goa_object_peek_exchange (object);
  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  server = goa_exchange_get_host (exchange);

  ews_client = goa_ews_client_new ();
  ret = goa_ews_client_autodiscover_sync (ews_client,
                                          email_address,
                                          password,
                                          username,
                                          server,
                                          accept_ssl_errors,
                                          cancellable,
                                          error);
  if (!ret)
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is the username
                           * (eg., debarshi.ray@gmail.com or rishi), and the
                           * (%s, %d) is the error domain and code.
                           */
                          _("Invalid password with username `%s' (%s, %d): "),
                          username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

 out:
  if (ews_client != NULL)
    g_object_unref (ews_client);
  g_free (password);
  if (credentials != NULL)
    g_variant_unref (credentials);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *grid1,
           GtkWidget     *grid2,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_vexpand (label, TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_container_add (GTK_CONTAINER (grid1), label);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_widget_set_vexpand (entry, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_max_length (GTK_ENTRY (entry), 132);
  gtk_container_add (GTK_CONTAINER (grid2), entry);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_entry != NULL)
    *out_entry = entry;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *spinner_button;

  GtkWidget *email_address;
  GtkWidget *password;

  GtkWidget *expander;
  GtkWidget *username;
  GtkWidget *server;

  gchar *account_object_path;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_valid_email_address (const gchar *email, gchar **out_username, gchar **out_domain)
{
  gchar *at;

  if (email == NULL || email[0] == '\0')
    return FALSE;

  at = strchr (email, '@');
  if (at == NULL || *(at + 1) == '\0')
    return FALSE;

  if (out_username != NULL)
    {
      *out_username = g_strdup (email);
      (*out_username)[at - email] = '\0';
    }

  if (out_domain != NULL)
    *out_domain = g_strdup (at + 1);

  return TRUE;
}

static void
on_email_address_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add;
  const gchar *email;
  gchar *domain;
  gchar *url;
  gchar *username;

  can_add = FALSE;
  domain = NULL;
  url = NULL;
  username = NULL;

  email = gtk_entry_get_text (GTK_ENTRY (data->email_address));
  if (!is_valid_email_address (email, &username, &domain))
    goto out;

  if (data->username != NULL)
    gtk_entry_set_text (GTK_ENTRY (data->username), username);

  if (data->server != NULL)
    gtk_entry_set_text (GTK_ENTRY (data->server), domain);

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (url);
  g_free (domain);
  g_free (username);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           gboolean        new_account,
                           AddAccountData *data)
{
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *grid2;
  GtkWidget *hbox;
  gint width;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  data->cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_hexpand (data->cluebar, TRUE);
  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (grid0), data->cluebar);

  data->cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (data->cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  grid1 = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid1), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);

  grid2 = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid2), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid2), 12);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_set_homogeneous (GTK_BOX (hbox), FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), grid1, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), grid2, TRUE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER (grid0), hbox);

  add_entry (grid1, grid2, _("_E-mail"), &data->email_address);
  add_entry (grid1, grid2, _("_Password"), &data->password);
  if (new_account)
    {
      data->expander = gtk_expander_new_with_mnemonic (_("_Custom"));
      gtk_expander_set_expanded (GTK_EXPANDER (data->expander), FALSE);
      gtk_expander_set_resize_toplevel (GTK_EXPANDER (data->expander), TRUE);
      gtk_container_add (GTK_CONTAINER (grid0), data->expander);

      grid1 = gtk_grid_new ();
      gtk_orientable_set_orientation (GTK_ORIENTABLE (grid1), GTK_ORIENTATION_VERTICAL);
      gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);

      grid2 = gtk_grid_new ();
      gtk_orientable_set_orientation (GTK_ORIENTABLE (grid2), GTK_ORIENTATION_VERTICAL);
      gtk_grid_set_row_spacing (GTK_GRID (grid2), 12);

      hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_box_set_homogeneous (GTK_BOX (hbox), FALSE);
      gtk_box_pack_start (GTK_BOX (hbox), grid1, FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (hbox), grid2, TRUE, TRUE, 0);
      gtk_container_add (GTK_CONTAINER (data->expander), hbox);

      add_entry (grid1, grid2, _("User_name"), &data->username);
      add_entry (grid1, grid2, _("_Server"), &data->server);
    }

  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus ((new_account) ? data->email_address : data->password);

  g_signal_connect (data->email_address, "changed", G_CALLBACK (on_email_address_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_email_address_or_password_changed), data);

  data->spinner_button = goa_spinner_button_new_from_stock (GTK_STOCK_CONNECT);
  gtk_dialog_add_action_widget (data->dialog, data->spinner_button, GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
  gtk_widget_set_size_request (GTK_WIDGET (data->dialog), width, -1);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

static void
autodiscover_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaEwsClient *client = GOA_EWS_CLIENT (source_object);
  AddAccountData *data = user_data;

  goa_ews_client_autodiscover_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
  goa_spinner_button_stop (GOA_SPINNER_BUTTON (data->spinner_button));
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaEwsClient *ews_client;
  GoaObject *ret;
  gboolean accept_ssl_errors;
  const gchar *email_address;
  const gchar *server;
  const gchar *password;
  const gchar *username;
  const gchar *provider_type;
  gint response;

  ews_client = NULL;
  accept_ssl_errors = FALSE;

  ret = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));

  ews_client = goa_ews_client_new ();

 ews_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  email_address = gtk_entry_get_text (GTK_ENTRY (data.email_address));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  server = gtk_entry_get_text (GTK_ENTRY (data.server));

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  username,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &data.error))
    goto out;

  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               NULL,
                               autodiscover_cb,
                               &data);
  goa_spinner_button_start (GOA_SPINNER_BUTTON (data.spinner_button));
  g_main_loop_run (data.loop);

  if (data.error != NULL)
    {
      gchar *markup;

      if (data.error->code == GOA_ERROR_SSL)
        {
          goa_spinner_button_set_label (GOA_SPINNER_BUTTON (data.spinner_button), _("_Ignore"));
          accept_ssl_errors = TRUE;
        }
      else
        {
          goa_spinner_button_set_label (GOA_SPINNER_BUTTON (data.spinner_button), _("_Try Again"));
          accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s:</b> %s",
                                _("Error connecting to Microsoft Exchange server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_expander_set_expanded (GTK_EXPANDER (data.expander), TRUE);
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto ews_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "Host", server);
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (accept_ssl_errors) ? "true" : "false");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                username,
                                email_address,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  /* We might have an object even when data.error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (ret != NULL);

  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  if (ews_client != NULL)
    g_object_unref (ews_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider    *provider,
                 GoaClient      *client,
                 GoaObject      *object,
                 GtkWindow      *parent,
                 GError        **error)
{
  AddAccountData data;
  GVariantBuilder builder;
  GoaAccount *account;
  GoaEwsClient *ews_client;
  GoaExchange *exchange;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean accept_ssl_errors;
  gboolean ret;
  const gchar *email_address;
  const gchar *server;
  const gchar *password;
  const gchar *username;
  gint response;

  g_return_val_if_fail (GOA_IS_EXCHANGE_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ews_client = NULL;
  ret = FALSE;

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (vbox), 12);

  memset (&data, 0, sizeof (AddAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = GTK_DIALOG (dialog);
  data.error = NULL;

  create_account_details_ui (provider, GTK_DIALOG (dialog), GTK_BOX (vbox), FALSE, &data);

  account = goa_object_peek_account (object);
  email_address = goa_account_get_presentation_identity (account);
  gtk_entry_set_text (GTK_ENTRY (data.email_address), email_address);
  gtk_editable_set_editable (GTK_EDITABLE (data.email_address), FALSE);

  gtk_widget_show_all (dialog);

  ews_client = goa_ews_client_new ();

 ews_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  username = goa_account_get_identity (account);

  exchange = goa_object_peek_exchange (object);
  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  server = goa_exchange_get_host (exchange);

  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               NULL,
                               autodiscover_cb,
                               &data);
  goa_spinner_button_start (GOA_SPINNER_BUTTON (data.spinner_button));
  g_main_loop_run (data.loop);

  if (data.error != NULL)
    {
      GtkWidget *button;
      gchar *markup;

      markup = g_strdup_printf ("<b>%s:</b> %s",
                                _("Error connecting to Microsoft Exchange server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      button = gtk_dialog_get_widget_for_response (data.dialog, GTK_RESPONSE_OK);
      gtk_button_set_label (GTK_BUTTON (button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto ews_again;
    }

  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    error))
    goto out;

  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  gtk_widget_destroy (dialog);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  if (ews_client != NULL)
    g_object_unref (ews_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *left,
              GtkGrid             *right)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_exchange_provider_parent_class)->show_account (provider,
                                                                         client,
                                                                         object,
                                                                         vbox,
                                                                         left,
                                                                         right);

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   _("Use for"),
                                                   "mail-disabled",
                                                   _("_Mail"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "calendar-disabled",
                                                   _("Cale_ndar"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "contacts-disabled",
                                                   _("_Contacts"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_exchange_provider_init (GoaExchangeProvider *provider)
{
}

static void
goa_exchange_provider_class_init (GoaExchangeProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *id,
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  GVariant *credentials;
  gchar *password;

  /* TODO: maybe log what app is requesting access */

  password = NULL;
  credentials = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));

  error = NULL;
  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   NULL, /* GCancellable* */
                                                   &error);
  if (credentials == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED, /* TODO: more specific */
                                             _("Did not find password with username `%s' in credentials"),
                                             id);
      goto out;
    }

  goa_password_based_complete_get_password (interface, invocation, password);

 out:
  g_free (password);
  if (credentials != NULL)
    g_variant_unref (credentials);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
