obj-$(CONFIG_DMA_SHARED_BUFFER) := dma-shared-buffer.o

dma-buf-objs-y := dma-buf.o fence.o reservation.o seqno-fence.o fence-array.o sw_sync.o
dma-buf-objs-$(CONFIG_SYNC_FILE)		+= sync_file.o

dma-shared-buffer-objs :=  $(dma-buf-objs-y)
