SHELL			:= bash
STD				:= c99

TARGET			:= ahabus-parser

SOURCE_DIR		:= ./src
BUILD_DIR		:= ./build
OBJECTS_DIR		:= $(BUILD_DIR)/intermediate
PRODUCT_DIR		:= $(BUILD_DIR)/product

SOURCES			:= $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS			:= $(patsubst $(SOURCE_DIR)/%.c, $(OBJECTS_DIR)/%.o, $(SOURCES))

CFLAGS			:= -Wall -std=$(STD)
LDFLAGS			:= 

.PHONY: clean

all: $(TARGET)

$(TARGET): $(PRODUCT_DIR) $(OBJECTS)
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $(PRODUCT_DIR)/$(TARGET)

$(OBJECTS_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "compiling $(notdir $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJECTS_DIR):
	@mkdir -p $(OBJECTS_DIR)
	
$(PRODUCT_DIR):
	@mkdir -p $(PRODUCT_DIR)

clean:
	@echo "cleaning project"
	@rm -rf $(BUILD_DIR)
	
