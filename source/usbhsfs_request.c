/*
 * usbhsfs_request.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * libusbhsfs is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * libusbhsfs is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"

#define USB_FEATURE_ENDPOINT_HALT   0x00

/* Type definitions. */

/// Imported from libusb, with changed names.
enum usb_request_type {
    USB_REQUEST_TYPE_STANDARD = (0x00 << 5),
    USB_REQUEST_TYPE_CLASS    = (0x01 << 5),
    USB_REQUEST_TYPE_VENDOR   = (0x02 << 5),
    USB_REQUEST_TYPE_RESERVED = (0x03 << 5),
};

/// Imported from libusb, with changed names.
enum usb_request_recipient {
    USB_RECIPIENT_DEVICE    = 0x00,
    USB_RECIPIENT_INTERFACE = 0x01,
    USB_RECIPIENT_ENDPOINT  = 0x02,
    USB_RECIPIENT_OTHER     = 0x03,
};

enum usb_bot_request {
    USB_REQUEST_BOT_GET_MAX_LUN = 0xFE,
    USB_REQUEST_BOT_RESET       = 0xFF
};

void *usbHsFsRequestAllocateCtrlXferBuffer(void)
{
    return memalign(USB_XFER_BUF_ALIGNMENT, USB_CTRL_XFER_BUFFER_SIZE);
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 7). */
Result usbHsFsRequestGetMaxLogicalUnits(UsbHsFsDriveContext *drive_ctx)
{
    Result rc = 0;
    u16 if_num = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;
    drive_ctx->max_lun = 1; /* Default value. */
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_GET_MAX_LUN, 0, if_num, 1, drive_ctx->ctrl_xfer_buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
        goto end;
    }
    
    if (xfer_size != 1)
    {
        USBHSFS_LOG("usbHsIfCtrlXfer read 0x%X byte(s), expected 0x%X!", xfer_size, 1);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }
    
    drive_ctx->max_lun = (*(drive_ctx->ctrl_xfer_buf) + 1);
    if (drive_ctx->max_lun > USB_BOT_MAX_LUN) drive_ctx->max_lun = 1;
    
end:
    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages 7 and 16). */
Result usbHsFsRequestMassStorageReset(UsbHsFsDriveContext *drive_ctx)
{
    Result rc = 0;
    u16 if_num = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    if_num = drive_ctx->usb_if_session.inf.inf.interface_desc.bInterfaceNumber;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_RESET, 0, if_num, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetEndpointStatus(UsbHsFsDriveContext *drive_ctx, bool out_ep, bool *out_status)
{
    Result rc = 0;
    u16 ep_addr = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx) || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    ep_addr = usb_ep_session->desc.bEndpointAddress;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_GET_STATUS, 0, ep_addr, 2, drive_ctx->ctrl_xfer_buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsIfCtrlXfer failed for %s endpoint! (0x%08X).", out_ep ? "output" : "input", rc);
        goto end;
    }
    
    if (xfer_size != 2)
    {
        USBHSFS_LOG("usbHsIfCtrlXfer got 0x%X byte(s) from %s endpoint, expected 0x%X!", xfer_size, out_ep ? "output" : "input", 2);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }
    
    *out_status = ((*((u16*)drive_ctx->ctrl_xfer_buf) & 0x01) != 0);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestClearEndpointHaltFeature(UsbHsFsDriveContext *drive_ctx, bool out_ep)
{
    Result rc = 0;
    u16 ep_addr = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    ep_addr = usb_ep_session->desc.bEndpointAddress;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, ep_addr, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfCtrlXfer failed for %s endpoint! (0x%08X).", out_ep ? "output" : "input", rc);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetDeviceConfiguration(UsbHsFsDriveContext *drive_ctx, u8 *out_conf)
{
    Result rc = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx) || !out_conf)
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_DEVICE, USB_REQUEST_GET_CONFIGURATION, 0, 0, 1, drive_ctx->ctrl_xfer_buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
        goto end;
    }
    
    if (xfer_size != 1)
    {
        USBHSFS_LOG("usbHsIfCtrlXfer read 0x%X byte(s), expected 0x%X!", xfer_size, 1);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }
    
    *out_conf = *(drive_ctx->ctrl_xfer_buf);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestSetDeviceConfiguration(UsbHsFsDriveContext *drive_ctx, u8 conf)
{
    Result rc = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_DEVICE, USB_REQUEST_SET_CONFIGURATION, conf, 0, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
    
end:
    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages: 19 - 22). */
Result usbHsFsRequestPostBuffer(UsbHsFsDriveContext *drive_ctx, bool out_ep, void *buf, u32 size, u32 *xfer_size, bool retry)
{
    Result rc = 0;
    bool status = false;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx) || !buf || !size || !xfer_size)
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    
    rc = usbHsEpPostBuffer(usb_ep_session, buf, size, xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsEpPostBuffer failed for %s endpoint! (0x%08X). Attempting to clear possible STALL status.", out_ep ? "output" : "input", rc);
        
        /* Check if the target endpoint was STALLed and if we can successfully clear the STALL status from it. Retry the transfer if needed. */
        if (R_SUCCEEDED(usbHsFsRequestGetEndpointStatus(drive_ctx, out_ep, &status)) && status && R_SUCCEEDED(usbHsFsRequestClearEndpointHaltFeature(drive_ctx, out_ep)))
        {
            if (retry)
            {
                rc = usbHsEpPostBuffer(usb_ep_session, buf, size, xfer_size);
                if (R_FAILED(rc)) USBHSFS_LOG("usbHsEpPostBuffer failed for %s endpoint! (retry) (0x%08X).", out_ep ? "output" : "input", rc);
            }
        }
    }
    
end:
    return rc;
}