#include "dma.hpp"

volatile uint32_t DMA::dma_buf[15] = { 0 };
volatile uint8_t DMA::chan_num = 0;
LPC_GPDMACH_TypeDef *DMA::chan = 0;
bool DMA::isInit = false;
static volatile uint32_t DMATCCount = 0;
static volatile uint32_t DMAErrCount = 0;

extern "C" void DMA_IRQHandler(void)
{
  uint32_t regVal;

  regVal = _LPC_DMA_ID->DMACIntTCStat;

  if (regVal) {
    DMATCCount++;
    _LPC_DMA_ID->DMACIntTCClear = regVal;

    ADCDMA::ADC_burst_off();
    // LPC_ADC->CR &= ~(0x1 << 16);  // Disable the BURST mode bit
  }


  regVal = _LPC_DMA_ID->DMACIntErrStat;

  if (regVal) {

    DMAErrCount++;
    _LPC_DMA_ID->DMACIntErrClr = regVal;

    ADCDMA::ADC_burst_off();
    // LPC_ADC->ADCR &= ~(0x1 << 16);  // Disable the BURST mode bit
  }
}


/**
 *
 */
DMA::DMA(void)
{
  isInit = false;
}


/**
 *
 */
DMA::~DMA(void)
{
  isInit = false;
}


/**
 * [DMA_Init description]
 */
bool DMA::Init(void)
{

  chan_num = find_channel();

  if (chan_num == 0xFF) {
    LOG(SEVERE, "No open DMA channels found.");
    return false;
  }

  LOG(INF2, "DMA channel found: Channel %u", chan_num);

  clear_int_flag(chan_num);
  clear_err_flag(chan_num);

  // Enable CLOCK into GPDMA controller
  LPC_SC->PCONP |= (1 << 29);

  // This won't be needed unless someone wants to code weird, crazy things
  // reset_periph_modes();
  // set_periph_mode(1);  // This sets it up for use with Timer0 match0 as being the selection

  // Sync enabled
  _LPC_DMA_ID->DMACSync |= 0xFFFF;

  // Startup the global DMA controller interface
  while (enable_controller(DMA_LITTLE_ENDIAN) < 0x01) { /* wait */ }

  // Setup everything, but don't start any transfers
  chan->DMACCControl = (
                         0x01  // Tranfer size - defined in the units set here.
                         | DMA_SET_SRC_SIZE(DMA_BURST_SIZE_1) // Set burst size to 1 unless there are 4 or 8 ADC transfers/request
                         | DMA_SET_DST_SIZE(DMA_BURST_SIZE_1)
                         | DMA_SET_SRC_WIDTH(DMA_WIDTH_WORD)
                         | DMA_SET_DST_WIDTH(DMA_WIDTH_WORD)
                         | DMA_SET_SRC_INCREMENT_MODE(DMA_ADDR_INCREMENT_NO)
                         | DMA_SET_DST_INCREMENT_MODE(DMA_ADDR_INCREMENT_NO)
                       );

  // Setup for use with ADC as the source by default
  set_src_periph(chan, DMA_ADC_REQ_ID);
  transfer_type(chan, 0x02);

  isInit = true;
  LOG(INF2, "DMA setup successfully completed!");

  return true;
}

void DMA::transfer_type(LPC_GPDMACH_TypeDef *dma_channel, uint8_t type)
{
  dma_channel->DMACCConfig |= (type << 11) & 0x07;
}


/**
 * [DMA::start description]
 */
bool DMA::Start(void)
{
  if (isInit == false)
    return false;

  // // Enable the channel once everything is properly configured
  enable_channel(chan);

  NVIC_EnableIRQ(DMA_IRQn);

  return true;
}


/**
 * [DMA::setSrc description]
 * @param addr [description]
 */
void DMA::SetSrc(uint32_t addr)
{
  if (isInit == false)
    return;

  chan->DMACCSrcAddr = (uint32_t)addr;
}


/**
 * [DMA::setDst description]
 * @param addr [description]
 */
void DMA::SetDst(uint32_t addr)
{
  if (isInit == false)
    return;

  chan->DMACCDestAddr = (uint32_t)addr;
}


/**
 * [DMA::find_channel description]
 * @return  [description]
 */
uint8_t DMA::find_channel(void)
{
  uint8_t channel;

  // Get a listing of channel states (available/unavailable)
  uint8_t enabled_channels = (_LPC_DMA_ID->DMACEnbldChns) & 0xFF;

  // Loop through all available channels until an open one is found
  for (channel = 0; channel < DMA_NUM_CHANNELS; channel++) {
    bool channel_taken = ((enabled_channels >> channel) & 0x01);

    if (channel_taken == false) {
      chan = (LPC_GPDMACH_TypeDef *)((uint32_t)_LPC_DMA_CHAN0 + (channel * sizeof(LPC_GPDMACH_TypeDef *)));
      return channel;
    }

  }

  return 0xFF;
}


/**
 * [DMA::enableController description]
 */
uint8_t DMA::enable_controller(bool endianness)
{
  _LPC_DMA_ID->DMACConfig |= (0x01 | (endianness << 1));
  return _LPC_DMA_ID->DMACConfig & 0x03;
}


/**
 * [DMA::disable_controller description]
 * @return  [description]
 */
uint8_t DMA::disable_controller(void)
{
  // Check for any enabled channels
  if (_LPC_DMA_ID->DMACEnbldChns & 0xFF)
    disable_channels();

  // Shutdown the DMA controller
  _LPC_DMA_ID->DMACConfig &= ~(0x01);

  return _LPC_DMA_ID->DMACConfig & 0x03;
}


/**
 * [DMA::clear_error description]
 * @param channel [description]
 */
void DMA::clear_error(uint8_t channel)
{
  _LPC_DMA_ID->DMACIntErrClr = ( 1 << channel );
}


/**
 * [DMA::clear_errors description]
 */
void DMA::clear_errors(void)
{
  for (int i = 0; i < DMA_NUM_CHANNELS; i++)
    clear_error(i);
}


/**
 * [DMA::enable_channel description]
 * @param dma_channel [description]
 */
void DMA::enable_channel(LPC_GPDMACH_TypeDef *dma_channel)
{
  dma_channel->DMACCConfig |= 0x01;
}


/**
 * [DMA::enable_channel description]
 * @param dma_channel [description]
 */
void DMA::disable_channel(LPC_GPDMACH_TypeDef *dma_channel)
{
  dma_channel->DMACCConfig &= ~(0x01);
}


/**
 * [DMA::disable_channels description]
 */
void DMA::disable_channels(void)
{
  for (int i = 0; i < DMA_NUM_CHANNELS; i++)
    disable_channel(_LPC_DMA_CHAN0 + i);
}


/**
 * [DMA::set_periph_mode description]
 * @param bit [description]
 */
void DMA::set_periph_mode(uint8_t mode)
{
  LPC_SC->DMAREQSEL |= (1 << mode);
}


/**
 * [DMA::reset_periph_modes description]
 */
void DMA::reset_periph_modes(void)
{
  LPC_SC->DMAREQSEL = 0x00;
}


/**
 * [DMA::set_src_periph description]
 */
void DMA::set_src_periph(LPC_GPDMACH_TypeDef *dma_channel, uint8_t mode)
{
  dma_channel->DMACCConfig |= ((mode << 1) & 0x0F);
}


/**
 * [DMA::set_dst_periph description]
 */
void DMA::set_dst_periph(LPC_GPDMACH_TypeDef *dma_channel, uint8_t mode)
{
  dma_channel->DMACCConfig |= ((mode << 6) & 0x0F);
}


/**
 * [DMA::clear_int_flags description]
 */
void DMA::clear_int_flag(uint8_t channel_num)
{
  // doesn't work yet!
  _LPC_DMA_ID->DMACIntTCClear = ( 1 << channel_num );
}



/**
 * [DMA::clear_err_flags description]
 */
void DMA::clear_err_flag(uint8_t channel_num)
{
  // doesn't work yet!
  _LPC_DMA_ID->DMACIntErrClr = ( 1 << channel_num );
}
