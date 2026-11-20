/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : App/usbd_desc.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB device descriptors.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */

/** @addtogroup USBD_DESC
  * @{
  */

/** @defgroup USBD_DESC_Private_TypesDefinitions USBD_DESC_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Defines USBD_DESC_Private_Defines
  * @brief Private defines.
  * @{
  */

#ifndef DSY_USB_DESC_MFR_STR
#define DSY_USB_DESC_MFR_STR "Synthux"
#endif
#ifndef DSY_USB_DESC_PRODUCT_STR
#define DSY_USB_DESC_PRODUCT_STR "Audrey III"
#endif

#define USBD_VID     1155
#define USBD_LANGID_STRING     1033
//#define USBD_MANUFACTURER_STRING     "Synthux"
#define USBD_MANUFACTURER_STRING     DSY_USB_DESC_MFR_STR
#define USBD_PID_FS     57105
//#define USBD_PRODUCT_STRING_FS     "Audrey III"
#define USBD_PRODUCT_STRING_FS     DSY_USB_DESC_PRODUCT_STR
#define USBD_CONFIGURATION_STRING_FS     "DFU Config"
#define USBD_INTERFACE_STRING_FS     "DFU Interface"

#define USBD_PID_HS     57105
//#define USBD_PRODUCT_STRING_HS     "Audrey III"
#define USBD_PRODUCT_STRING_HS     (DSY_USB_DESC_PRODUCT_STR)
#define USBD_CONFIGURATION_STRING_HS     "DFU Config"
#define USBD_INTERFACE_STRING_HS     "DFU Interface"

/* USER CODE BEGIN PRIVATE_DEFINES */

/* Windows (Audrey build only, USBD_CLASS_BOS_ENABLED=1 in audrey/Makefile): USB 2.01
   with a BOS descriptor that points to Microsoft OS 2.0 descriptors. Windows 8.1 and
   later then install WinUSB for update mode by themselves, so Chrome's WebUSB can
   update Audrey with no Zadig step. Its own release number (3.00), so Windows doesn't
   reuse what it learned about another 0483:DF11 device such as ST's ROM loader.
   See ../specs/audrey-bootloader.md, "Windows". */
#if (USBD_CLASS_BOS_ENABLED == 1)
#define USBD_BCDUSB_LO       0x01 /* bcdUSB 2.01 */
#define USBD_BCDDEVICE_HI    0x03 /* bcdDevice 3.00 */
#define MS_OS_20_VENDOR_CODE 0x20 /* bRequest of Windows' vendor request */
#define MS_OS_20_SET_LEN     0xA2 /* 10 header + 20 compatible ID + 132 registry */
#define MS_OS_20_DESCRIPTOR_INDEX 0x07
#else
#define USBD_BCDUSB_LO       0x00 /* bcdUSB 2.00 */
#define USBD_BCDDEVICE_HI    0x02 /* bcdDevice 2.00 */
#endif

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/** @defgroup USBD_DESC_Private_Macros USBD_DESC_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_FunctionPrototypes USBD_DESC_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t * pbuf, uint8_t len);

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_FunctionPrototypes USBD_DESC_Private_FunctionPrototypes
  * @brief Private functions declaration for FS.
  * @{
  */

uint8_t * USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
#if (USBD_CLASS_BOS_ENABLED == 1)
uint8_t * USBD_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_MsOs20Descriptor(uint8_t vendor_code, uint16_t index, uint16_t *length);
#endif

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Variables USBD_DESC_Private_Variables
  * @brief Private variables.
  * @{
  */

USBD_DescriptorsTypeDef FS_Desc =
{
  USBD_FS_DeviceDescriptor
, USBD_FS_LangIDStrDescriptor
, USBD_FS_ManufacturerStrDescriptor
, USBD_FS_ProductStrDescriptor
, USBD_FS_SerialStrDescriptor
, USBD_FS_ConfigStrDescriptor
, USBD_FS_InterfaceStrDescriptor
#if (USBD_CLASS_BOS_ENABLED == 1)
, USBD_BOSDescriptor
#endif
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/** USB standard device descriptor. */
__ALIGN_BEGIN uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
  0x12,                       /*bLength */
  USB_DESC_TYPE_DEVICE,       /*bDescriptorType*/
  USBD_BCDUSB_LO,             /*bcdUSB 2.00, or 2.01 with the BOS descriptor */
  0x02,
  0x00,                       /*bDeviceClass*/
  0x00,                       /*bDeviceSubClass*/
  0x00,                       /*bDeviceProtocol*/
  USB_MAX_EP0_SIZE,           /*bMaxPacketSize*/
  LOBYTE(USBD_VID),           /*idVendor*/
  HIBYTE(USBD_VID),           /*idVendor*/
  LOBYTE(USBD_PID_FS),        /*idProduct*/
  HIBYTE(USBD_PID_FS),        /*idProduct*/
  0x00,                       /*bcdDevice rel. 2.00, or 3.00 with the BOS descriptor */
  USBD_BCDDEVICE_HI,
  USBD_IDX_MFC_STR,           /*Index of manufacturer  string*/
  USBD_IDX_PRODUCT_STR,       /*Index of product string*/
  USBD_IDX_SERIAL_STR,        /*Index of serial number string*/
  USBD_MAX_NUM_CONFIGURATION  /*bNumConfigurations*/
};

// /** @defgroup USBD_DESC_Private_FunctionPrototypes USBD_DESC_Private_FunctionPrototypes
//   * @brief Private functions declaration for HS.
//   * @{
//   */

uint8_t * USBD_HS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_HS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

// /**
//   * @}
//   */

// /** @defgroup USBD_DESC_Private_Variables USBD_DESC_Private_Variables
//   * @brief Private variables.
//   * @{
//   */

USBD_DescriptorsTypeDef HS_Desc =
{
  USBD_HS_DeviceDescriptor
, USBD_HS_LangIDStrDescriptor
, USBD_HS_ManufacturerStrDescriptor
, USBD_HS_ProductStrDescriptor
, USBD_HS_SerialStrDescriptor
, USBD_HS_ConfigStrDescriptor
, USBD_HS_InterfaceStrDescriptor
#if (USBD_CLASS_BOS_ENABLED == 1)
, USBD_BOSDescriptor
#endif
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/** USB standard device descriptor. */
__ALIGN_BEGIN uint8_t USBD_HS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
  0x12,                       /*bLength */
  USB_DESC_TYPE_DEVICE,       /*bDescriptorType*/
  USBD_BCDUSB_LO,             /*bcdUSB 2.00, or 2.01 with the BOS descriptor */

  0x02,
  0x00,                       /*bDeviceClass*/
  0x00,                       /*bDeviceSubClass*/
  0x00,                       /*bDeviceProtocol*/
  USB_MAX_EP0_SIZE,           /*bMaxPacketSize*/
  LOBYTE(USBD_VID),           /*idVendor*/
  HIBYTE(USBD_VID),           /*idVendor*/
  LOBYTE(USBD_PID_HS),        /*idProduct*/
  HIBYTE(USBD_PID_HS),        /*idProduct*/
  0x00,                       /*bcdDevice rel. 2.00, or 3.00 with the BOS descriptor */
  USBD_BCDDEVICE_HI,
  USBD_IDX_MFC_STR,           /*Index of manufacturer  string*/
  USBD_IDX_PRODUCT_STR,       /*Index of product string*/
  USBD_IDX_SERIAL_STR,        /*Index of serial number string*/
  USBD_MAX_NUM_CONFIGURATION  /*bNumConfigurations*/
};

/* USB_DeviceDescriptor */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Variables USBD_DESC_Private_Variables
  * @brief Private variables.
  * @{
  */

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */

/** USB lang identifier descriptor. */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC,
     USB_DESC_TYPE_STRING,
     LOBYTE(USBD_LANGID_STRING),
     HIBYTE(USBD_LANGID_STRING)
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/* Internal string descriptor. */
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

#if defined ( __ICCARM__ ) /*!< IAR Compiler */
  #pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
  USB_SIZ_STRING_SERIAL,
  USB_DESC_TYPE_STRING,
};

#if (USBD_CLASS_BOS_ENABLED == 1)
/* BOS descriptor: one platform capability, Microsoft OS 2.0. It tells Windows the
   size of the descriptor set and which vendor request returns it. */
__ALIGN_BEGIN static uint8_t USBD_BOSDesc[0x21] __ALIGN_END =
{
  0x05,                       /* bLength */
  USB_DESC_TYPE_BOS,          /* bDescriptorType */
  0x21, 0x00,                 /* wTotalLength: 5 + 28 */
  0x01,                       /* bNumDeviceCaps */

  0x1C,                       /* bLength */
  0x10,                       /* bDescriptorType: device capability */
  0x05,                       /* bDevCapabilityType: platform */
  0x00,                       /* bReserved */
  0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, /* PlatformCapabilityUUID, */
  0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F, /* {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F} */
  0x00, 0x00, 0x03, 0x06,     /* dwWindowsVersion: Windows 8.1 (0x06030000) */
  LOBYTE(MS_OS_20_SET_LEN), HIBYTE(MS_OS_20_SET_LEN), /* wMSOSDescriptorSetTotalLength */
  MS_OS_20_VENDOR_CODE,       /* bMS_VendorCode */
  0x00                        /* bAltEnumCode: none */
};

/* Microsoft OS 2.0 descriptor set, for the whole device (it has one interface):
   use WinUSB, and register a device interface GUID so programs such as Chrome can
   open it. The GUID is Audrey's own and must never change. */
__ALIGN_BEGIN static uint8_t USBD_MsOs20Set[MS_OS_20_SET_LEN] __ALIGN_END =
{
  /* Set header */
  0x0A, 0x00,                 /* wLength */
  0x00, 0x00,                 /* wDescriptorType: MS_OS_20_SET_HEADER_DESCRIPTOR */
  0x00, 0x00, 0x03, 0x06,     /* dwWindowsVersion: Windows 8.1 */
  LOBYTE(MS_OS_20_SET_LEN), HIBYTE(MS_OS_20_SET_LEN), /* wTotalLength */

  /* Compatible ID: WINUSB */
  0x14, 0x00,                 /* wLength */
  0x03, 0x00,                 /* wDescriptorType: MS_OS_20_FEATURE_COMPATBLE_ID */
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,       /* CompatibleID */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SubCompatibleID */

  /* Registry property: DeviceInterfaceGUIDs (REG_MULTI_SZ) */
  0x84, 0x00,                 /* wLength: 132 */
  0x04, 0x00,                 /* wDescriptorType: MS_OS_20_FEATURE_REG_PROPERTY */
  0x07, 0x00,                 /* wPropertyDataType: REG_MULTI_SZ */
  0x2A, 0x00,                 /* wPropertyNameLength: 42 */
  'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
  'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0,
  0, 0,
  0x50, 0x00,                 /* wPropertyDataLength: 80 */
  '{', 0, 'E', 0, 'D', 0, '8', 0, '1', 0, '1', 0, 'D', 0, '0', 0, '7', 0, '-', 0,
  '8', 0, '8', 0, 'C', 0, '0', 0, '-', 0, '4', 0, '3', 0, '6', 0, '9', 0, '-', 0,
  '8', 0, 'E', 0, 'F', 0, 'C', 0, '-', 0, '6', 0, 'A', 0, '2', 0, '7', 0, '3', 0,
  'C', 0, '0', 0, '0', 0, '3', 0, 'A', 0, '5', 0, '5', 0, '}', 0, 0, 0, 0, 0
};
#endif /* USBD_CLASS_BOS_ENABLED */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Functions USBD_DESC_Private_Functions
  * @brief Private functions.
  * @{
  */

/**
  * @brief  Return the device descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_FS_DeviceDesc);
  return USBD_FS_DeviceDesc;
}

/**
  * @brief  Return the LangID string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

/**
  * @brief  Return the product string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the manufacturer string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

/**
  * @brief  Return the serial number string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = USB_SIZ_STRING_SERIAL;

  /* Update the serial number string descriptor with the data from the unique
   * ID */
  Get_SerialNum();
  /* USER CODE BEGIN USBD_FS_SerialStrDescriptor */

  /* USER CODE END USBD_FS_SerialStrDescriptor */
  return (uint8_t *) USBD_StringSerial;
}

/**
  * @brief  Return the configuration string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == USBD_SPEED_HIGH)
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the interface string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the device descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_HS_DeviceDesc);
  return USBD_HS_DeviceDesc;
}

/**
  * @brief  Return the LangID string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

/**
  * @brief  Return the product string descriptor
  * @param  speed : current device speed
  * @param  length : pointer to data length variable
  * @retval pointer to descriptor buffer
  */
uint8_t * USBD_HS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the manufacturer string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

/**
  * @brief  Return the serial number string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = USB_SIZ_STRING_SERIAL;

  /* Update the serial number string descriptor with the data from the unique
   * ID */
  Get_SerialNum();
  /* USER CODE BEGIN USBD_HS_SerialStrDescriptor */

  /* USER CODE END USBD_HS_SerialStrDescriptor */

  return (uint8_t *) USBD_StringSerial;
}

/**
  * @brief  Return the configuration string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == USBD_SPEED_HIGH)
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the interface string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_HS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

#if (USBD_CLASS_BOS_ENABLED == 1)
/**
  * @brief  Return the BOS descriptor (Microsoft OS 2.0 platform capability)
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_BOSDesc);
  return USBD_BOSDesc;
}

/**
  * @brief  The Microsoft OS 2.0 descriptor set, for Windows' vendor request
  *         (bmRequestType 0xC0, bRequest MS_OS_20_VENDOR_CODE, wIndex 7)
  * @param  vendor_code : bRequest of the request
  * @param  index : wIndex of the request
  * @param  length : Pointer to data length variable
  * @retval Pointer to the set, or NULL when the request is not this one
  */
uint8_t * USBD_MsOs20Descriptor(uint8_t vendor_code, uint16_t index, uint16_t *length)
{
  if (vendor_code != MS_OS_20_VENDOR_CODE || index != MS_OS_20_DESCRIPTOR_INDEX)
  {
    *length = 0U;
    return NULL;
  }
  *length = sizeof(USBD_MsOs20Set);
  return USBD_MsOs20Set;
}
#endif /* USBD_CLASS_BOS_ENABLED */

/**
  * @brief  Create the serial number string descriptor
  * @param  None
  * @retval None
  */
static void Get_SerialNum(void)
{
  uint32_t deviceserial0, deviceserial1, deviceserial2;

  deviceserial0 = *(uint32_t *) DEVICE_ID1;
  deviceserial1 = *(uint32_t *) DEVICE_ID2;
  deviceserial2 = *(uint32_t *) DEVICE_ID3;

  deviceserial0 += deviceserial2;

  if (deviceserial0 != 0)
  {
    IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8);
    IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4);
  }
}

/**
  * @brief  Convert Hex 32Bits value into char
  * @param  value: value to convert
  * @param  pbuf: pointer to the buffer
  * @param  len: buffer length
  * @retval None
  */
static void IntToUnicode(uint32_t value, uint8_t * pbuf, uint8_t len)
{
  uint8_t idx = 0;

  for (idx = 0; idx < len; idx++)
  {
    if (((value >> 28)) < 0xA)
    {
      pbuf[2 * idx] = (value >> 28) + '0';
    }
    else
    {
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;
    }

    value = value << 4;

    pbuf[2 * idx + 1] = 0;
  }
}
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
