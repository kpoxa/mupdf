/*
 * pdfextract -- the ultimate way to extract images and fonts from pdfs
 */

#include "mupdf.h"
#include "mupdf-internal.h"

static pdf_document *doc = NULL;
static fz_context *ctx = NULL;
static int dorgb = 0;
static int fonts_only = 0;
static int images_only = 0;

static void usage(void)
{
	fprintf(stderr, "usage: mutool extract [options] file.pdf [object numbers]\n");
	fprintf(stderr, "\t-p\tpassword\n");
	fprintf(stderr, "\t-r\tconvert images to rgb\n");
	fprintf(stderr, "\t-f\textract only fonts\n");
	fprintf(stderr, "\t-i\textract only images\n");
	exit(1);
}

static int isimage(pdf_obj *obj)
{
	pdf_obj *type = pdf_dict_gets(obj, "Subtype");
	return pdf_is_name(type) && !strcmp(pdf_to_name(type), "Image");
}

static int isfontdesc(pdf_obj *obj)
{
	pdf_obj *type = pdf_dict_gets(obj, "Type");
	return pdf_is_name(type) && !strcmp(pdf_to_name(type), "FontDescriptor");
}

static void saveimage(int num)
{
	fz_image *image;
	fz_pixmap *img;
	pdf_obj *ref;
	char name[32];

	ref = pdf_new_indirect(ctx, num, 0, doc);

	/* TODO: detect DCTD and save as jpeg */

	image = pdf_load_image(doc, ref);
	img = fz_image_to_pixmap(ctx, image, 0, 0);
	fz_drop_image(ctx, image);

	sprintf(name, "img-%04d", num);
	fz_write_pixmap(ctx, img, name, dorgb);

	fz_drop_pixmap(ctx, img);
	pdf_drop_obj(ref);
}

static void savefont(pdf_obj *dict, int num)
{
	char name[1024];
	char *subtype;
	fz_buffer *buf;
	pdf_obj *stream = NULL;
	pdf_obj *obj;
	char *ext = "";
	FILE *f;
	char *fontname = "font";
	int n, len;
	unsigned char *data;

	obj = pdf_dict_gets(dict, "FontName");
	if (obj)
		fontname = pdf_to_name(obj);

	obj = pdf_dict_gets(dict, "FontFile");
	if (obj)
	{
		stream = obj;
		ext = "pfa";
	}

	obj = pdf_dict_gets(dict, "FontFile2");
	if (obj)
	{
		stream = obj;
		ext = "ttf";
	}

	obj = pdf_dict_gets(dict, "FontFile3");
	if (obj)
	{
		stream = obj;

		obj = pdf_dict_gets(obj, "Subtype");
		if (obj && !pdf_is_name(obj))
			fz_throw(ctx, "Invalid font descriptor subtype");

		subtype = pdf_to_name(obj);
		if (!strcmp(subtype, "Type1C"))
			ext = "cff";
		else if (!strcmp(subtype, "CIDFontType0C"))
			ext = "cid";
		else if (!strcmp(subtype, "OpenType"))
			ext = "otf";
		else
			fz_throw(ctx, "Unhandled font type '%s'", subtype);
	}

	if (!stream)
	{
		fz_warn(ctx, "Unhandled font type");
		return;
	}

	buf = pdf_load_stream(doc, pdf_to_num(stream), pdf_to_gen(stream));

	sprintf(name, "%s-%04d.%s", fontname, num, ext);
	printf("extracting font %s\n", name);

	f = fopen(name, "wb");
	if (!f)
		fz_throw(ctx, "Error creating font file");

	len = fz_buffer_storage(ctx, buf, &data);
	n = fwrite(data, 1, len, f);
	if (n < len)
		fz_throw(ctx, "Error writing font file");

	if (fclose(f) < 0)
		fz_throw(ctx, "Error closing font file");

	fz_drop_buffer(ctx, buf);
}

static void showobject(int num)
{
	pdf_obj *obj;

	if (!doc)
		fz_throw(ctx, "no file specified");

	obj = pdf_load_object(doc, num, 0);

	if (isimage(obj) && !fonts_only)
		saveimage(num);
	else if (isfontdesc(obj) && !images_only)
		savefont(obj, num);

	pdf_drop_obj(obj);
}

int pdfextract_main(int argc, char **argv)
{
	char *infile;
	char *password = "";
	int c, o;

	while ((c = fz_getopt(argc, argv, "p:rfi")) != -1)
	{
		switch (c)
		{
		case 'p': password = fz_optarg; break;
		case 'r': dorgb++; break;
		case 'f': fonts_only = 1; break;
		case 'i': images_only = 1; break;
		default: usage(); break;
		}
	}

	if (fz_optind == argc)
		usage();

	infile = argv[fz_optind++];

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}

	doc = pdf_open_document_no_run(ctx, infile);
	if (pdf_needs_password(doc))
		if (!pdf_authenticate_password(doc, password))
			fz_throw(ctx, "cannot authenticate password: %s", infile);

	if (fz_optind == argc)
	{
		int len = pdf_count_objects(doc);
		for (o = 0; o < len; o++)
			showobject(o);
	}
	else
	{
		while (fz_optind < argc)
		{
			showobject(atoi(argv[fz_optind]));
			fz_optind++;
		}
	}

	pdf_close_document(doc);
	fz_flush_warnings(ctx);
	fz_free_context(ctx);
	return 0;
}
