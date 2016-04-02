<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:wix="http://schemas.microsoft.com/wix/2006/wi">

  <xsl:template name="matchTranslationFile">
    <xsl:param name="filename" />
    <xsl:value-of select="(contains($filename, 'qtbase_') or contains($filename, 'qt_')) and not(contains($filename, 'qt_help_'))"/>
  </xsl:template>

  <xsl:template match="node()|@*">
    <xsl:copy>
      <xsl:apply-templates select="node()|@*" />
    </xsl:copy>
  </xsl:template>

  <xsl:template match="wix:Component">
    <xsl:variable name="matched">
      <xsl:call-template name="matchTranslationFile">
        <xsl:with-param name="filename" select="./wix:File/@Source" />
      </xsl:call-template>
    </xsl:variable>
    <xsl:if test="$matched = 'true'">
      <xsl:copy>
        <xsl:apply-templates select="node()|@*" />
      </xsl:copy>
    </xsl:if>
  </xsl:template>

  <xsl:template match="wix:ComponentRef">
    <xsl:variable name="id" select="@Id" />
    <xsl:variable name="matched">
      <xsl:for-each select="//wix:Component[@Id = $id]">
        <xsl:call-template name="matchTranslationFile">
          <xsl:with-param name="filename" select="./wix:File/@Source" />
        </xsl:call-template>
      </xsl:for-each>
    </xsl:variable>
    <xsl:if test="$matched = 'true'">
      <xsl:copy>
        <xsl:apply-templates select="node()|@*" />
      </xsl:copy>
    </xsl:if>
  </xsl:template>

</xsl:stylesheet>
