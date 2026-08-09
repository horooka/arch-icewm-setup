return {
	{
		"LazyVim/LazyVim",
		opts = {
			colorscheme = function()
				require("catppuccin").setup({
					flavour = "mocha",
					transparent_background = false,
					term_colors = true,
					color_overrides = {
						mocha = {
							base = "#000000",
							mantle = "#000000",
							crust = "#000000",
						},
					},
					styles = {
						comments = {},
						conditionals = {},
						loops = {},
						functions = {},
						keywords = {},
						strings = {},
						variables = {},
						numbers = {},
						booleans = {},
						properties = {},
						types = {},
					},
					integrations = {
						telescope = { enabled = true, style = "nvchad" },
						dropbar = { enabled = true, color_mode = true },
					},
				})
				vim.cmd.colorscheme("catppuccin")
			end,
		},
	},
	{
		"catppuccin/nvim",
		name = "catppuccin",
		priority = 1000,
	},
}
