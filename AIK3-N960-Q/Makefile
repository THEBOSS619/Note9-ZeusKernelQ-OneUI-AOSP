NAME ?= RZ

DATE := $(shell date +'%Y%m%d-%H%M')

ZIP := $(NAME)-$(DEVICE)-$(DATE).zip

EXCLUDE := \
	Makefile README.md LICENSE make.sh *.git* "$(NAME)-"*.zip* \

all: $(ZIP)

$(ZIP):
	@echo "Creating ZIP: $(ZIP)"
	@zip -r9 "$@" . -x $(EXCLUDE)
	@echo "Done."

clean:
	@rm -vf "$(NAME)-"*.zip*
	@rm -f Image dt*.img
	@echo "Done."
